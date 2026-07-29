#!/usr/bin/env python3
"""Fleet alarm daemon -- watches the bsd CSV logs and raises email + PC-popup alarms.

Pairs with http_server.py (the fleet log sink): that appends every device's POSTed
row to <log_dir>/<device>.csv; this tails those files and fires an alarm when a row
matches a configured rule. Three rule kinds:

  * event   -- a named column equals a value (e.g. WaterLeak's event column == "LEAK").
  * low_vcc -- the last column (the fleet-uniform vcc_mV) drops below a per-device
               threshold. ONLY devices listed in the config's allowlist are checked,
               so mains-powered devices (IrrCntrl, PowerMonitor) whose last column is
               not a battery voltage are never mis-flagged.
  * stale   -- no row at all for longer than a per-device limit. This is the one that
               catches a dead device: a pack going flat does NOT produce a low reading,
               it browns the module out mid-wake and the posts simply stop, so SILENCE
               is the only signal there is. Devices that would otherwise be silent for
               months (WaterLeak sleeps until it gets wet) emit a periodic heartbeat row
               for this rule to watch; set the limit to ~2x their heartbeat interval so
               one missed report -- an AP reboot, a failed post -- is not an alarm.

An alarm sends an email (Gmail SMTP + app password) and GETs each Windows popup
listener (alarm_listener.py) to raise a persistent banner. Every alarm is rate-limited
per (device, rule) so a device that keeps re-triggering (standing water, flat battery
posting each wake) does not spam. Stdlib only; run as an rc.d service on bsd.

    python3 fleet_alarm.py [--config /usr/local/etc/fleet_alarm.json] [--once]

--once runs a single scan and exits (for testing); default loops every poll_s seconds.
On first run it seeds offsets to end-of-file so history is not re-alarmed.
"""

import argparse
import fnmatch
import json
import os
import signal
import subprocess
import sys
import time
import urllib.parse
import urllib.request


def log(msg):
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}", flush=True)


def load_config(path):
    with open(path) as f:
        return json.load(f)


class State:
    """Per-file read offsets + per-(device,rule) last-alarm times, persisted to JSON.

    offsets creep forward on nearly every poll, so saving unconditionally rewrote this
    file once per poll_s -- on ZFS that dirties a whole record in every transaction
    group, for a few bytes of moved offset. save() therefore coalesces: it writes at
    most once per flush_s, but ALWAYS writes through immediately when last_alarm
    changed. That keeps alarm rate-limiting exact across a restart; the only thing a
    stale offset costs is re-reading up to flush_s of already-seen rows, and any alarm
    they re-trigger is suppressed by the (already persisted) rate limiter.
    """

    def __init__(self, path, flush_s=60):
        self.path = path
        self.flush_s = flush_s
        self.offsets = {}
        self.last_alarm = {}
        if path and os.path.exists(path):
            try:
                d = json.load(open(path))
                self.offsets = d.get("offsets", {})
                self.last_alarm = d.get("last_alarm", {})
            except Exception as e:
                log(f"WARN could not read state {path}: {e}")
        self._saved = self._payload()
        self._saved_alarms = json.dumps(self.last_alarm, sort_keys=True)
        self._saved_at = time.monotonic()

    def _payload(self):
        return json.dumps({"offsets": self.offsets, "last_alarm": self.last_alarm},
                          sort_keys=True)

    def save(self, force=False):
        if not self.path:
            return
        payload = self._payload()
        if payload == self._saved:
            return
        alarms = json.dumps(self.last_alarm, sort_keys=True)
        if not (force or alarms != self._saved_alarms or
                time.monotonic() - self._saved_at >= self.flush_s):
            return
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            f.write(payload)
        os.replace(tmp, self.path)
        self._saved = payload
        self._saved_alarms = alarms
        self._saved_at = time.monotonic()


def matches_any(name, globs):
    return any(fnmatch.fnmatch(name, g) for g in globs)


def threshold_for(device, low):
    """(threshold_mV, scale) for an allowlisted device, or (None, 1). The threshold is
    always in ACTUAL battery mV; `scale` converts a device's raw last-column reading to
    actual mV (e.g. Soil32 reports a 1/2-divider value -> scale 2). A device value may be
    a bare int (threshold, scale 1) or {"threshold":X, "scale":Y}."""
    for glob, spec in low.get("devices", {}).items():
        if fnmatch.fnmatch(device, glob):
            if isinstance(spec, dict):
                return spec.get("threshold"), spec.get("scale", 1)
            return spec, 1
    return None, 1


def stale_limit_for(device, stale):
    """Max silence in seconds for an allowlisted device, or None (device not watched).
    A device value may be a bare number (seconds) or {"max_silence_s": X}."""
    for glob, spec in stale.get("devices", {}).items():
        if fnmatch.fnmatch(device, glob):
            if isinstance(spec, dict):
                return spec.get("max_silence_s")
            return spec
    return None


def send_email(cfg, subject, body):
    """Hand the message to bsd's configured mailer (sendmail -> ssmtp). We do NOT own
    any SMTP/credentials here -- the system mailer is already set up; -t reads the
    recipients from the To: header."""
    m = cfg.get("mail", {})
    to = m.get("to", [])
    if not to:
        log("no mail.to configured; skipping email")
        return
    frm = m.get("from", "fleet_alarm@bsd")
    sendmail = m.get("sendmail", "/usr/sbin/sendmail")
    raw = f"From: {frm}\nTo: {', '.join(to)}\nSubject: {subject}\n\n{body}\n"
    try:
        p = subprocess.run([sendmail, "-t"], input=raw.encode(),
                           timeout=30, capture_output=True)
        if p.returncode == 0:
            log(f"email sent: {subject}")
        else:
            log(f"ERROR sendmail rc={p.returncode}: {p.stderr.decode(errors='replace')[:200]}")
    except Exception as e:
        log(f"ERROR sendmail: {e}")


def raise_popup(cfg, message, color, seconds):
    for target in cfg.get("popup_targets", []):
        q = urllib.parse.urlencode({"msg": message, "color": color, "secs": seconds})
        url = f"http://{target}/alarm?{q}"
        try:
            urllib.request.urlopen(url, timeout=5).read()
            log(f"popup -> {target}")
        except Exception as e:
            log(f"popup {target} failed: {e}")  # PC may be off -- email still went


def fire(cfg, state, device, rule_key, rate_s, subject, body, popup_msg, color, secs):
    key = f"{device}|{rule_key}"
    now = time.time()
    last = state.last_alarm.get(key, 0)
    if now - last < rate_s:
        return  # rate-limited
    state.last_alarm[key] = now
    log(f"ALARM {subject}")
    send_email(cfg, subject, body)
    raise_popup(cfg, popup_msg, color, secs)


def handle_line(cfg, state, device, line):
    parts = [c.strip() for c in line.split(",")]
    if len(parts) < 2:
        return

    for rule in cfg.get("rules", {}).get("event", []):
        if not fnmatch.fnmatch(device + ".csv", rule["file_glob"]):
            continue
        col = rule["column"]  # index into the row (0 = server timestamp)
        if col < len(parts) and parts[col] == rule["equals"]:
            msg = rule.get("message", "{device}: {value}").format(device=device, value=parts[col])
            fire(cfg, state, device, "event:" + rule["equals"], rule.get("rate_limit_s", 300),
                 subject=msg, body=f"{msg}\nrow: {line}",
                 popup_msg=msg, color=rule.get("color", "#b00020"), secs=rule.get("popup_seconds", 0))

    low = cfg.get("rules", {}).get("low_vcc", {})
    if low.get("enabled"):
        thr, scale = threshold_for(device, low)
        if thr is not None:
            try:
                vcc = int(round(int(parts[-1]) * scale))  # -> actual battery mV
            except ValueError:
                return
            if vcc < thr:
                msg = f"LOW BATTERY: {device} {vcc} mV (< {thr})"
                fire(cfg, state, device, "low_vcc", low.get("rate_limit_s", 86400),
                     subject=msg, body=f"{msg}\nrow: {line}",
                     popup_msg=msg, color=low.get("color", "#b06a00"),
                     secs=low.get("popup_seconds", 0))


def check_stale(cfg, state):
    """Alarm on devices that have gone silent. Unlike the row rules this reads no content --
    the file's mtime IS the last-heard time, since http_server.py only ever appends.

    A device that has never posted has no file and so is never flagged: there is nothing to
    have gone stale, and inventing an expected-device list here would fire on every device
    that is legitimately retired. The rate limiter (persisted in the state file) is what keeps
    a device that stays dead from mailing on every poll, and keeps a daemon restart quiet."""
    stale = cfg.get("rules", {}).get("stale", {})
    if not stale.get("enabled"):
        return
    log_dir, now = cfg["log_dir"], time.time()
    for fn in sorted(os.listdir(log_dir)):
        if not fn.endswith(".csv") or fn.endswith("-header.csv"):
            continue
        device = fn[:-4]
        max_s = stale_limit_for(device, stale)
        if not max_s:
            continue
        try:
            age = now - os.path.getmtime(os.path.join(log_dir, fn))
        except OSError:
            continue
        if age < max_s:
            continue
        msg = f"SILENT: {device}, no report in {age / 86400:.1f} days"
        fire(cfg, state, device, "stale", stale.get("rate_limit_s", 86400),
             subject=msg, body=f"{msg} (expected at least every {max_s / 86400:.1f} days)",
             popup_msg=msg, color=stale.get("color", "#b06a00"),
             secs=stale.get("popup_seconds", 0))


def watched_globs(cfg):
    globs = [r["file_glob"] for r in cfg.get("rules", {}).get("event", [])]
    globs += list(cfg.get("rules", {}).get("low_vcc", {}).get("devices", {}).keys())
    # low_vcc device keys are device-name globs; match against the .csv filename too
    return globs + [g + ".csv" for g in globs]


def scan(cfg, state, seed):
    log_dir = cfg["log_dir"]
    globs = watched_globs(cfg)
    for fn in sorted(os.listdir(log_dir)):
        if not fn.endswith(".csv") or fn.endswith("-header.csv"):
            continue
        if not matches_any(fn, globs):
            continue
        path = os.path.join(log_dir, fn)
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        off = state.offsets.get(fn, None)
        if seed or off is None:
            state.offsets[fn] = size  # start at EOF; don't alarm on history
            continue
        if off > size:  # rotated/truncated
            off = 0
        if off == size:
            continue
        device = fn[:-4]
        with open(path, "r", errors="replace") as f:
            f.seek(off)
            for line in f:
                line = line.rstrip("\n")
                if line:
                    handle_line(cfg, state, device, line)
            state.offsets[fn] = f.tell()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="/usr/local/etc/fleet_alarm.json")
    ap.add_argument("--once", action="store_true")
    args = ap.parse_args()

    cfg = load_config(args.config)
    state = State(cfg.get("state_file"), cfg.get("state_flush_s", 60))
    poll_s = cfg.get("poll_s", 5)

    seed = not state.offsets  # first run: seed offsets to EOF
    if seed:
        log("first run: seeding offsets to end-of-file (history not alarmed)")
    scan(cfg, state, seed)
    # Unlike the row rules, staleness IS checked on the seeding run: a device that is
    # already dead is exactly what we want to hear about at startup, and history cannot
    # re-trigger it (mtime is a single current fact, and fire() is rate-limited).
    check_stale(cfg, state)
    state.save(force=True)
    if args.once:
        return

    # Flush the coalesced offsets on a clean stop (service stop / reboot) so a
    # restart resumes where it left off instead of re-reading the last flush_s.
    def _flush_and_exit(signum, frame):
        state.save(force=True)
        sys.exit(0)

    signal.signal(signal.SIGTERM, _flush_and_exit)
    signal.signal(signal.SIGINT, _flush_and_exit)

    log(f"fleet_alarm watching {cfg['log_dir']} every {poll_s}s")
    while True:
        time.sleep(poll_s)
        try:
            scan(cfg, state, False)
            check_stale(cfg, state)
            state.save()
        except Exception as e:
            log(f"ERROR scan: {e}")


if __name__ == "__main__":
    main()

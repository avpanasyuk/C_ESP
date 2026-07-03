#!/usr/bin/env python3
"""Fleet alarm daemon -- watches the bsd CSV logs and raises email + PC-popup alarms.

Pairs with http_server.py (the fleet log sink): that appends every device's POSTed
row to <log_dir>/<device>.csv; this tails those files and fires an alarm when a row
matches a configured rule. Two rule kinds:

  * event   -- a named column equals a value (e.g. WaterLeak's event column == "LEAK").
  * low_vcc -- the last column (the fleet-uniform vcc_mV) drops below a per-device
               threshold. ONLY devices listed in the config's allowlist are checked,
               so mains-powered devices (IrrCntrl, PowerMonitor) whose last column is
               not a battery voltage are never mis-flagged.

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
    """Per-file read offsets + per-(device,rule) last-alarm times, persisted to JSON."""

    def __init__(self, path):
        self.path = path
        self.offsets = {}
        self.last_alarm = {}
        if path and os.path.exists(path):
            try:
                d = json.load(open(path))
                self.offsets = d.get("offsets", {})
                self.last_alarm = d.get("last_alarm", {})
            except Exception as e:
                log(f"WARN could not read state {path}: {e}")

    def save(self):
        if not self.path:
            return
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"offsets": self.offsets, "last_alarm": self.last_alarm}, f)
        os.replace(tmp, self.path)


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
    state = State(cfg.get("state_file"))
    poll_s = cfg.get("poll_s", 5)

    seed = not state.offsets  # first run: seed offsets to EOF
    if seed:
        log("first run: seeding offsets to end-of-file (history not alarmed)")
    scan(cfg, state, seed)
    state.save()
    if args.once:
        return

    log(f"fleet_alarm watching {cfg['log_dir']} every {poll_s}s")
    while True:
        time.sleep(poll_s)
        try:
            scan(cfg, state, False)
            state.save()
        except Exception as e:
            log(f"ERROR scan: {e}")


if __name__ == "__main__":
    main()

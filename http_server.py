#!/usr/bin/env python3
"""
HTTP server for the ESP fleet.

POST  /<anypath>  body: filename-or-email,csv,data,...
                  If filename-or-email contains '@' it's treated as an email
                  recipient: the SECOND CSV field is the subject and the rest
                  is the body. Otherwise (default) the row is appended to
                  <log-dir>/<filename> with a sortable, human-readable local
                  timestamp prepended: 'YYYY-MM-DD HH:MM:SS.hh' (hundredth-second).
                  Parses directly in Python and MATLAB and converts back to epoch.

GET   /firmware/<name>.bin                       -> serves <firmware-dir>/<name>.bin. Returns
                                                    304 (device skips re-flashing) when the
                                                    device's sketch MD5 matches md5(<name>.bin).
                                                    Used by ESP8266 ESPhttpUpdate / ESP32
                                                    httpUpdate for cheap polling.

The update library sends the running sketch's MD5 on every poll (x-ESP8266-sketch-md5 on
ESP8266, x-ESP32-sketch-md5 on ESP32 -- both accepted), and md5(<name>.bin) equals that for
a matching image -- so it's the reliable "already running this?" test. The library does NOT
send If-Modified-Since, so the mtime check is only a fallback. The version string
(x-ESP8266-version / x-ESP32-version) is logged for visibility.

Email mode requires a working `mail(1)` binary in PATH and a local MTA
(sendmail/postfix) that can relay or deliver. Tested on FreeBSD bsd.
"""

import argparse
import email.utils
import hashlib
import os
import re
import subprocess
import sys
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import HTTPServer, ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path

# Fleet OTA confirm/blacklist state (sibling module). Optional: if it can't import, the
# server runs exactly as before -- OTA-safety recording just goes dark.
try:
    import fleet_ota
except ImportError:
    fleet_ota = None


EMAIL_RE = re.compile(r"^[^@\s,]+@[^@\s,]+\.[^@\s,]+$")


def looks_like_email(s):
    """True if `s` looks like an email address (no whitespace/commas, has @ and a dot in the domain)."""
    return bool(EMAIL_RE.match(s))


def send_mail(recipient, subject, body, mail_bin="mail"):
    """Pipe `body` to `<mail_bin> -s <subject> <recipient>`. Raises CalledProcessError on failure."""
    # Sanitize subject: collapse any newlines (header-injection guard) and cap length.
    subject = subject.replace("\n", " ").replace("\r", " ")[:200]
    subprocess.run(
        [mail_bin, "-s", subject, recipient],
        input=body.encode("utf-8"),
        check=True,
        timeout=30,
    )


# A log whose filename already carries the month has a natural boundary: the device
# starts a fresh file on the 1st, so the file is bounded by the month and size-rotating
# it only splits one month across several files -- which hands every reader a chance to
# see part of a month and believe it saw all of it. Such names are exempt from the size
# cap; logs without a month in the name (EVR_Balance.log, IrrCntrl.csv, Debug_log.csv)
# grow without bound and keep it. Matches a ".MM.YY." group, '.', '_' or '-' separated,
# e.g. PowerMonitor.v0.09.26.main.csv.
MONTHLY_NAME_RE = re.compile(r'(?:^|[._-])(?:0[1-9]|1[0-2])[._-]\d{2}(?:[._-]|$)')

# Rotation/rate state. Requests run on their own threads (ThreadingHTTPServer), so the
# append-then-maybe-rotate sequence needs a lock per file: without one, two concurrent
# POSTs can both see an over-size file and rotate it twice, stranding a one-row chunk.
_locks_meta = threading.Lock()
_locks = {}
_rate_lock = threading.Lock()
_rate_windows = {}      # filename -> deque of monotonic arrival times, trimmed to 60 s
_alerts_sent = set()    # alert keys already emailed; issue-only, never a heartbeat
_dir_checked_at = [0.0]


def _lock_for(path):
    key = str(path)
    with _locks_meta:
        lock = _locks.get(key)
        if lock is None:
            lock = _locks[key] = threading.Lock()
    return lock


def rotate_log(csv_path, when):
    """Rename `csv_path` aside so a fresh file starts, and return the new path.

    NON-DESTRUCTIVE by construction: the suffix is a timestamp, so a rotation can never
    land on an existing file (and a second rotation inside the same second still gets a
    counter). Readers reassemble a series by reading `<name>` together with its
    `<name>.<stamp>` siblings, which sort chronologically as plain strings.

    An earlier scheme reused a single `.1` and let `Path.replace` overwrite it, which
    silently destroyed every chunk but the last two -- about two thirds of several
    months of meter data, while returning 200 to the device and logging nothing lost.
    """
    stamp = when.strftime('%Y%m%d-%H%M%S')
    target = Path(f"{csv_path}.{stamp}")
    n = 0
    while target.exists():
        n += 1
        target = Path(f"{csv_path}.{stamp}-{n}")
    csv_path.replace(target)
    return target


def alert_once(key, subject, body, recipient, mail_bin):
    """Email `recipient` the first time `key` fires; repeats are silent until restart.

    Never raises into the request path -- an unreachable MTA must not cost a log row.
    """
    if not recipient or key in _alerts_sent:
        return
    _alerts_sent.add(key)
    try:
        send_mail(recipient, subject, body, mail_bin=mail_bin)
    except Exception as e:                                # noqa: BLE001 - never fatal
        print(f"  alert email failed: {e}")


def rate_ok(filename, limit):
    """True if `filename` is under `limit` rows/minute. 0/None disables the limit.

    This, not the size cap, is the real guard against a device filling the disk: a
    runaway is a RATE anomaly. A device on a sane cadence posts a few rows a minute and
    cannot reach the limit, so nothing legitimate is ever dropped.
    """
    if not limit:
        return True
    now = time.monotonic()
    with _rate_lock:
        window = _rate_windows.setdefault(filename, deque())
        while window and now - window[0] > 60.0:
            window.popleft()
        if len(window) >= limit:
            return False
        window.append(now)
        return True


def check_dir_quota(log_dir, quota, recipient, mail_bin):
    """Backstop: alert once if the log dir passes `quota` bytes. Never deletes anything.

    Rescanned at most every 5 minutes -- stat'ing every file on each POST would make the
    sink's per-row cost grow with its own history.
    """
    if not quota:
        return
    now = time.monotonic()
    if now - _dir_checked_at[0] < 300.0:
        return
    _dir_checked_at[0] = now
    try:
        total = sum(e.stat().st_size for e in os.scandir(log_dir) if e.is_file())
    except OSError:
        return
    if total > quota:
        alert_once('dir-quota', f"log sink over quota ({total} bytes)",
                   f"{log_dir} holds {total} bytes, past the {quota}-byte threshold.\n"
                   "Nothing has been deleted. Free space or raise --dir-quota-bytes.",
                   recipient, mail_bin)


class ESPDataHandler(BaseHTTPRequestHandler):
    log_dir = None
    firmware_dir = None
    ota = None            # fleet_ota.FleetOTA instance, or None (OTA-safety disabled)
    mail_bin = "mail"
    # Past this size a log is renamed aside with a timestamp suffix and a fresh one
    # starts. This bounds a single FILE, not the series -- nothing is ever discarded.
    # 0 disables rotation entirely.
    max_log_bytes = 10 * 1024 * 1024
    # Filenames matching this are never size-rotated whatever max_log_bytes says,
    # because the name already carries the month (see MONTHLY_NAME_RE). None makes
    # every file obey max_log_bytes.
    no_rotate_re = MONTHLY_NAME_RE
    # Rows/minute accepted per filename; excess is dropped with a 429 and one alert.
    # This is what actually bounds disk use. 0 disables.
    max_rows_per_min = 120
    # Alert once if the log dir passes this many bytes. Never deletes. 0 disables.
    dir_quota_bytes = 0
    # Where rate-limit and quota alerts go. None disables alerting (they still print).
    alert_email = None
    # -v: echo each accepted POST. Off by default -- the row is already stored
    # verbatim (same timestamp) in the CSV under log_dir, so echoing duplicates it.
    verbose = False
    # Per-request socket timeout: a client that opens a connection but never sends
    # a complete request is dropped after this many seconds instead of blocking the
    # worker. Paired with ThreadingHTTPServer below so one stuck/half-open client
    # (e.g. a crash-looping ESP) can never wedge the whole sink.
    timeout = 15

    # -- POST: data logging or email ---------------------------------------------------

    def do_POST(self):
        try:
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            data = body.decode('utf-8').strip()

            # Prepend a sortable, human-readable LOCAL timestamp to logged rows:
            #   'YYYY-MM-DD HH:MM:SS.hh'  (hundredth-second, truncated)
            # Sorts lexicographically = chronologically, and round-trips to epoch:
            #   Python: datetime.strptime(s, '%Y-%m-%d %H:%M:%S.%f').timestamp()
            #   MATLAB: posixtime(datetime(s,'InputFormat','yyyy-MM-dd HH:mm:ss.SS', ...
            #                              'TimeZone','local'))
            # (both parse in the server's local zone). Console/email keep ms precision.
            now = datetime.now()
            ts_csv = now.strftime('%Y-%m-%d %H:%M:%S.%f')[:-4]   # hundredths: CSV log
            ts_human = now.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]  # ms: console + email
            parts = [p.strip() for p in data.split(',')]

            if not parts:
                print(f"\n[{ts_human}] Error: empty data")
                self._send(400, b'Error: Empty data')
                return

            first = parts[0]
            csv_data = parts[1:]

            # Email dispatch path: parts[0] is an email address.
            if looks_like_email(first):
                if not csv_data or not csv_data[0]:
                    print(f"\n[{ts_human}] Email to {first}: missing subject (2nd CSV field)")
                    self._send(400, b'Email needs subject as second CSV field')
                    return
                subject = csv_data[0]
                body_data = csv_data[1:]
                mail_body = f"{ts_human}\n" + (",".join(body_data) if body_data else "")
                print(f"\n[{ts_human}] Email -> {first}")
                print(f"  Subject: {subject!r}")
                print(f"  Body: {mail_body!r}")
                try:
                    send_mail(first, subject, mail_body, mail_bin=self.mail_bin)
                    print(f"  Sent")
                    self._send(200, b'OK')
                except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError) as e:
                    print(f"  Email send failed: {e}")
                    self._send(500, b'Email send failed')
                return

            # Default path: append to file. The filename comes from the POST
            # body, so confine it to log_dir (reject path traversal).
            filename = first
            csv_path = (Path(self.log_dir) / filename).resolve()
            try:
                csv_path.relative_to(Path(self.log_dir).resolve())
            except ValueError:
                print(f"\n[{ts_human}] Rejected out-of-dir filename: {filename!r}")
                self._send(400, b'Bad filename')
                return
            if self.verbose:
                print(f"\n[{ts_human}] Received data:")
                print(f"  File: {filename}")
                print(f"  Data: {', '.join(csv_data)}")
                print(f"  Size: {len(data)} bytes")

            if not rate_ok(filename, self.max_rows_per_min):
                print(f"[{ts_human}] Rate limit: dropped a row for {filename}")
                alert_once(f"rate:{filename}",
                           f"log sink dropping rows from {filename}",
                           f"{filename} passed {self.max_rows_per_min} rows/min at "
                           f"{ts_human}; excess rows are being dropped so it cannot fill "
                           f"the disk. A device on a sane cadence cannot reach this rate "
                           f"-- suspect a crash loop or a stuck retry.",
                           self.alert_email, self.mail_bin)
                self._send(429, b'Rate limited')
                return

            try:
                csv_path.parent.mkdir(parents=True, exist_ok=True)
                with _lock_for(csv_path):
                    with open(csv_path, 'a') as f:
                        f.write(','.join([ts_csv] + csv_data) + '\n')
                    exempt = self.no_rotate_re is not None and self.no_rotate_re.search(filename)
                    if self.max_log_bytes and not exempt \
                            and csv_path.stat().st_size > self.max_log_bytes:
                        target = rotate_log(csv_path, now)
                        print(f"[{ts_human}] Rotated {filename} -> {target.name} "
                              f"(> {self.max_log_bytes} bytes)")
                check_dir_quota(self.log_dir, self.dir_quota_bytes,
                                self.alert_email, self.mail_bin)
                if self.verbose:
                    print(f"  Written to: {csv_path.absolute()}")
            except IOError as e:
                print(f"[{ts_human}] Error writing CSV {csv_path}: {e}")

            # Fleet OTA confirm: a device's BOOT row carries "md5=<hex>" (its running image)
            # and, in the FleetServerDebug format "<file>,<name>,<line>", csv_data[0] is the
            # device name. That the row arrived proves the image booted far enough to raise
            # WiFi and post -> record it as healthy so the watchdog won't revert it. Never
            # let this break the log write.
            if self.ota is not None and fleet_ota is not None and csv_data:
                try:
                    md5 = fleet_ota.extract_md5_token(csv_data)
                    if md5:
                        device = csv_data[0]
                        name = fleet_ota.firmware_name_from_device(device)
                        self.ota.record_confirm(name, md5, device)
                        print(f"[{ts_human}] [ota] confirm {name} md5={md5[:8]} by {device}")
                except Exception as e:
                    print(f"[{ts_human}] [ota] confirm recording failed: {e}")

            self._send(200, b'OK')

        except Exception as e:
            print(f"Error processing POST: {e}")
            self._send(500, b'Error')

    # -- GET: firmware serving for ESPhttpUpdate ---------------------------------------

    def do_GET(self):
        if self.firmware_dir is None or not self.path.startswith('/firmware/'):
            self._send(404, b'Not Found')
            return

        # Sanitize: strip /firmware/ prefix, reject path-traversal attempts
        rel = self.path[len('/firmware/'):]
        if '..' in rel.split('/') or rel.startswith('/'):
            self._send(400, b'Bad Request')
            return

        path = (Path(self.firmware_dir) / rel).resolve()
        firmware_root = Path(self.firmware_dir).resolve()
        try:
            path.relative_to(firmware_root)
        except ValueError:
            self._send(400, b'Bad Request')
            return

        if not path.is_file():
            print(f"[firmware] GET {self.path} -> 404 (not on disk)")
            self._send(404, b'Not Found')
            return

        file_mtime = datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc)
        # truncate to whole seconds (HTTP date precision)
        file_mtime = file_mtime.replace(microsecond=0)

        # The raw header doubles as proof the client IS an ESP, so keep it separate from the
        # '?' used for display -- see the pull-recording guard below.
        version_hdr = (self.headers.get('x-ESP8266-version')
                       or self.headers.get('x-ESP32-version'))
        device_version = version_hdr or '?'

        name = path.stem          # "<NAME>.bin" -> "<NAME>" (matches the watchdog)
        file_md5 = hashlib.md5(path.read_bytes()).hexdigest()

        # Fleet OTA blacklist guard: an image whose md5 was reverted as bad must never be
        # served again (a re-drop of the same bytes). Answer 304 "no update" so the device
        # keeps its current image. Fail open -- a state error must not block firmware.
        if self.ota is not None and fleet_ota is not None:
            try:
                if self.ota.is_blacklisted(name, file_md5):
                    print(f"[firmware] GET {self.path} -> 304 (BLACKLISTED md5={file_md5[:8]})")
                    self.send_response(304)
                    self.send_header('Last-Modified', email.utils.format_datetime(file_mtime, usegmt=True))
                    self.end_headers()
                    return
            except Exception as e:
                print(f"[firmware] [ota] blacklist check failed (serving anyway): {e}")

        # Primary dedup: the update library sends the running sketch's MD5 on every
        # poll, and md5(<name>.bin) equals that for a matching image -- an equal
        # MD5 means the device already runs this firmware. (The library does NOT
        # send If-Modified-Since, so the date check below is only a fallback.)
        # ESP8266 (ESPhttpUpdate) sends x-ESP8266-sketch-md5; ESP32 (httpUpdate)
        # sends x-ESP32-sketch-md5 -- accept either so dedup works for both.
        device_md5 = (self.headers.get('x-ESP8266-sketch-md5')
                      or self.headers.get('x-ESP32-sketch-md5'))
        if device_md5 and device_md5 == file_md5:
            print(f"[firmware] GET {self.path} (device fw={device_version}) -> 304 (md5 match)")
            self.send_response(304)
            self.send_header('Last-Modified', email.utils.format_datetime(file_mtime, usegmt=True))
            self.end_headers()
            return

        ims_header = self.headers.get('If-Modified-Since')
        if ims_header:
            try:
                ims = email.utils.parsedate_to_datetime(ims_header)
                if ims.tzinfo is None:
                    ims = ims.replace(tzinfo=timezone.utc)
                if ims >= file_mtime:
                    print(f"[firmware] GET {self.path} (device fw={device_version}) -> 304 Not Modified")
                    self.send_response(304)
                    self.send_header('Last-Modified', email.utils.format_datetime(file_mtime, usegmt=True))
                    self.end_headers()
                    return
            except (TypeError, ValueError) as e:
                print(f"[firmware] Bad If-Modified-Since '{ims_header}': {e}")

        size = path.stat().st_size
        print(f"[firmware] GET {self.path} (device fw={device_version}) -> 200 OK, {size} bytes")

        # Fleet OTA: record that this device is about to flash file_md5, so the watchdog can
        # tell a deployed-and-pulled image (must confirm) from one no device has fetched yet
        # (leave alone). Device id = its STA MAC (stable), else chip id / sketch md5 / version.
        #
        # ONLY a client that identifies itself as an ESP starts the confirm clock. A bare curl
        # or browser GET is a human checking the route, not a deployment -- recording it as a
        # pull makes the watchdog revert AND blacklist a healthy image once CONFIRM_WINDOW_S
        # expires, and a blacklisted md5 is answered 304 forever, so re-dropping the same bytes
        # silently never deploys. Serving the bytes to anyone is fine; only the pull record is.
        if self.ota is not None and fleet_ota is not None:
            device = (self.headers.get('x-ESP8266-STA-MAC')
                      or self.headers.get('x-ESP32-STA-MAC')
                      or self.headers.get('x-ESP8266-Chip-ID')
                      or self.headers.get('x-ESP32-Chip-ID')
                      or (f"md5={device_md5[:8]}" if device_md5 else None)
                      or (f"fw={version_hdr}" if version_hdr else None))
            if device is None:
                print("  [ota] anonymous GET (no ESP headers) -- not recorded as a pull")
            else:
                try:
                    self.ota.record_pull(name, file_md5, device)
                    print(f"  [ota] pull {name} md5={file_md5[:8]} by {device}")
                except Exception as e:
                    print(f"  [ota] pull recording failed: {e}")

        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size))
        self.send_header('Last-Modified', email.utils.format_datetime(file_mtime, usegmt=True))
        self.end_headers()
        with open(path, 'rb') as f:
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                self.wfile.write(chunk)

    # -- shared helpers ----------------------------------------------------------------

    def _send(self, code, body):
        self.send_response(code)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass  # we do our own logging


def main():
    parser = argparse.ArgumentParser(
        description='HTTP server: receive POST data from ESP fleet, serve firmware to ESPhttpUpdate',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
POST  body format (file mode): filename,col1,col2,...
  Writes <timestamp>,col1,col2,... to <log-dir>/<filename>

POST  body format (email mode): address@host.dom,subject,body...
  Sends an email to the address. Subject is the second CSV field;
  body is timestamp + remaining fields (joined by comma).
  Requires `mail(1)` and a working local MTA.

GET   /firmware/<name>.bin
  Serves <firmware-dir>/<name>.bin; 304 when the device's sketch MD5 (x-ESP8266- or x-ESP32-) already
  matches md5(<name>.bin). Drop a fresh .bin and the next polling device picks it up.
''',
    )
    parser.add_argument('-p', '--port', type=int, default=8000)
    parser.add_argument('-H', '--host', default='0.0.0.0')
    parser.add_argument('-D', '--dir', default='/mnt/T',
                        help='Directory where logged CSVs land (default: /mnt/T)')
    parser.add_argument('-F', '--firmware-dir', default=None,
                        help='Directory where firmware .bin files live; omit to disable GET /firmware/')
    parser.add_argument('--mail-bin', default='mail',
                        help='Path to mail(1) binary used for email-mode POSTs (default: mail)')
    parser.add_argument('--max-log-bytes', type=int, default=10 * 1024 * 1024,
                        help='Rename a log aside as <file>.<YYYYmmdd-HHMMSS> once it exceeds '
                             'this size (default: 10 MiB, 0 disables). Nothing is ever deleted. '
                             'Does not apply to names matching --no-rotate-pattern.')
    parser.add_argument('--no-rotate-pattern', default=MONTHLY_NAME_RE.pattern,
                        help='Regex on the posted filename; a match is never size-rotated, '
                             'because a name carrying the month already has a natural boundary '
                             'and splitting it hides part of a month from readers. Empty string '
                             'exempts nothing.')
    parser.add_argument('--max-rows-per-min', type=int, default=120,
                        help='Rows/minute accepted per filename; excess is dropped with a 429 '
                             'and one alert (default: 120, 0 disables). This is the guard '
                             'against a crash-looping device filling the disk.')
    parser.add_argument('--dir-quota-bytes', type=int, default=0,
                        help='Alert once if the log dir passes this many bytes. Never deletes '
                             '(default: 0 = off)')
    parser.add_argument('--alert-email', default=None,
                        help='Recipient for rate-limit and quota alerts (default: none)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Echo every accepted POST to stdout. Off by default: the CSV row '
                             'already holds the same data and timestamp, so this only duplicates it.')
    args = parser.parse_args()

    ESPDataHandler.log_dir = args.dir
    ESPDataHandler.firmware_dir = args.firmware_dir
    ESPDataHandler.mail_bin = args.mail_bin
    ESPDataHandler.max_log_bytes = args.max_log_bytes
    try:
        ESPDataHandler.no_rotate_re = re.compile(args.no_rotate_pattern) \
            if args.no_rotate_pattern else None
    except re.error as e:
        parser.error(f"--no-rotate-pattern is not a valid regex: {e}")
    ESPDataHandler.max_rows_per_min = args.max_rows_per_min
    ESPDataHandler.dir_quota_bytes = args.dir_quota_bytes
    ESPDataHandler.alert_email = args.alert_email
    ESPDataHandler.verbose = args.verbose

    if args.firmware_dir and not os.path.isdir(args.firmware_dir):
        print(f"Warning: firmware-dir '{args.firmware_dir}' does not exist", file=sys.stderr)

    # Enable fleet OTA confirm/blacklist recording when serving firmware. Optional: if the
    # sibling module is missing the server still logs + serves exactly as before.
    if args.firmware_dir and fleet_ota is not None:
        ESPDataHandler.ota = fleet_ota.FleetOTA(args.firmware_dir)
        print(f"  fleet OTA:    on (state {fleet_ota.STATE_BASENAME}, "
              f"confirm window {fleet_ota.CONFIRM_WINDOW_S}s)")
    else:
        print(f"  fleet OTA:    off ({'no firmware dir' if not args.firmware_dir else 'fleet_ota.py missing'})")

    # ThreadingHTTPServer: each request runs in its own (daemon) thread, so one
    # slow/stuck/half-open client cannot block all the others (the single-threaded
    # HTTPServer would wedge the whole sink in that case).
    server = ThreadingHTTPServer((args.host, args.port), ESPDataHandler)
    server.daemon_threads = True
    print(f"HTTP server listening on http://{args.host}:{args.port}")
    print(f"  log dir:      {args.dir}")
    print(f"  firmware dir: {args.firmware_dir or '(disabled)'}")
    print(f"  mail binary:  {args.mail_bin}")
    print(f"  max log size: {str(args.max_log_bytes) + ' bytes (rename aside, timestamped)'
                             if args.max_log_bytes else 'unlimited (no rotation)'}")
    print(f"  never rotate: {args.no_rotate_pattern or '(nothing exempt)'}")
    print(f"  rate limit:   {args.max_rows_per_min or 'off'} rows/min per file")
    print(f"  dir quota:    {args.dir_quota_bytes or 'off'}"
          f"{' bytes' if args.dir_quota_bytes else ''}")
    print(f"  alert email:  {args.alert_email or '(none)'}")
    print("Press Ctrl+C to stop\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped")
        server.server_close()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""
HTTP server for the ESP fleet.

POST  /<anypath>  body: filename-or-email,csv,data,...
                  If filename-or-email contains '@' it's treated as an email
                  recipient: the SECOND CSV field is the subject and the rest
                  is the body. Otherwise (default) the row is appended to
                  <log-dir>/<filename> with a Unix-epoch timestamp prepended
                  (matches the legacy readout_*.py CSV format so MATLAB
                  analysis of pre-existing files keeps working).

GET   /firmware/<name>.bin                       -> serves <firmware-dir>/<name>.bin. Returns
                                                    304 (device skips re-flashing) when the
                                                    device's x-ESP8266-sketch-md5 matches
                                                    md5(<name>.bin). Used by ESPhttpUpdate for
                                                    cheap polling.

ESPhttpUpdate sends x-ESP8266-sketch-md5 (the running sketch's MD5) on every poll, and
md5(<name>.bin) equals that for a matching image -- so it's the reliable "already running
this?" test. The library does NOT send If-Modified-Since, so the mtime check is only a
fallback. The x-ESP8266-version string is logged for visibility.

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
import time
from datetime import datetime, timezone
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path


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


class ESPDataHandler(BaseHTTPRequestHandler):
    log_dir = None
    firmware_dir = None
    mail_bin = "mail"
    max_log_bytes = 10 * 1024 * 1024   # rotate a log file to .1 once it grows past this

    # -- POST: data logging or email ---------------------------------------------------

    def do_POST(self):
        try:
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            data = body.decode('utf-8').strip()

            # Two timestamps: file mode uses Unix epoch (matches the legacy
            # readout_*.py CSV output so MATLAB analysis keeps working);
            # email mode uses human-readable (epoch in a notification is
            # unfriendly); console log lines also use human-readable.
            # Truncate epoch to 2 decimals (10 ms) -- 60-Hz sampling has no
            # need for the ~16-digit precision Python's default str() produces.
            ts_epoch = f"{time.time():.2f}"
            ts_human = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
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
            print(f"\n[{ts_human}] Received data:")
            print(f"  File: {filename}")
            print(f"  Data: {', '.join(csv_data)}")
            print(f"  Size: {len(data)} bytes")

            try:
                csv_path.parent.mkdir(parents=True, exist_ok=True)
                with open(csv_path, 'a') as f:
                    f.write(','.join([ts_epoch] + csv_data) + '\n')
                # Cap file size so a chatty/looping device can't fill the disk:
                # past the limit, rotate to <file>.1 (replacing any old .1) and
                # let a fresh file start. Bounds each log to ~2x max_log_bytes.
                if csv_path.stat().st_size > self.max_log_bytes:
                    csv_path.replace(Path(str(csv_path) + '.1'))
                    print(f"  Rotated {filename} (> {self.max_log_bytes} bytes)")
                print(f"  Written to: {csv_path.absolute()}")
            except IOError as e:
                print(f"  Error writing CSV: {e}")

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

        device_version = self.headers.get('x-ESP8266-version', '?')

        # Primary dedup: ESPhttpUpdate sends the running sketch's MD5 on every
        # poll, and md5(<name>.bin) equals that for a matching image -- an equal
        # MD5 means the device already runs this firmware. (The library does NOT
        # send If-Modified-Since, so the date check below is only a fallback.)
        device_md5 = self.headers.get('x-ESP8266-sketch-md5')
        if device_md5 and device_md5 == hashlib.md5(path.read_bytes()).hexdigest():
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
  Serves <firmware-dir>/<name>.bin; 304 when the device's x-ESP8266-sketch-md5 already
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
                        help='Rotate a log file to <file>.1 once it exceeds this size (default: 10 MiB)')
    args = parser.parse_args()

    ESPDataHandler.log_dir = args.dir
    ESPDataHandler.firmware_dir = args.firmware_dir
    ESPDataHandler.mail_bin = args.mail_bin
    ESPDataHandler.max_log_bytes = args.max_log_bytes

    if args.firmware_dir and not os.path.isdir(args.firmware_dir):
        print(f"Warning: firmware-dir '{args.firmware_dir}' does not exist", file=sys.stderr)

    server = HTTPServer((args.host, args.port), ESPDataHandler)
    print(f"HTTP server listening on http://{args.host}:{args.port}")
    print(f"  log dir:      {args.dir}")
    print(f"  firmware dir: {args.firmware_dir or '(disabled)'}")
    print(f"  mail binary:  {args.mail_bin}")
    print(f"  max log size: {args.max_log_bytes} bytes (rotate to .1)")
    print("Press Ctrl+C to stop\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped")
        server.server_close()


if __name__ == '__main__':
    main()

"""Fleet alarm popup listener -- run one instance per Windows PC.

bsd's fleet_alarm.py GETs this listener when a device raises an alarm (water leak,
low battery, ...):

    GET /alarm?msg=<text>&color=<hex>&secs=<seconds>

and this spawns a top-most banner (popup_banner.py). secs=0 -> persistent until
dismissed (the default for alarms). Generic: the message/color/duration come from
the query, so bsd controls what every alarm looks like -- no per-alarm code here.

Run:
    pythonw alarm_listener.py        # background, no console window
    python  alarm_listener.py        # foreground, prints events

Autostart: see README.md (Task Scheduler at logon). PORT must match the port in
bsd's fleet_alarm.json popup_targets ("<host>:<port>").
"""

import os
import subprocess
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8765

HERE = os.path.dirname(os.path.abspath(__file__))
BANNER = os.path.join(HERE, "popup_banner.py")

# Launch the banner with pythonw so no console flashes; fall back to python.exe.
PYW = os.path.join(os.path.dirname(sys.executable), "pythonw.exe")
if not os.path.exists(PYW):
    PYW = sys.executable

DETACHED = 0x00000008 | 0x08000000  # DETACHED_PROCESS | CREATE_NO_WINDOW


def show_banner(message, color, seconds):
    subprocess.Popen([PYW, BANNER, message, color, str(seconds)],
                     creationflags=DETACHED, close_fds=True)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != "/alarm":
            self.send_response(404)
            self.end_headers()
            return
        q = urllib.parse.parse_qs(parsed.query)
        msg = q.get("msg", ["Fleet alarm"])[0]
        color = q.get("color", ["#b00020"])[0]
        secs = q.get("secs", ["0"])[0]

        print(f"ALARM: {msg} (color={color}, secs={secs})", flush=True)
        show_banner(msg, color, secs)

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"OK")

    def log_message(self, *args):
        pass  # we print our own concise line


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print(f"Fleet alarm listener on :{port} (banner = {BANNER})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()

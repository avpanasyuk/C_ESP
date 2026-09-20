"""Functional test of http_server.py's log rotation, month-name exemption and rate limit.

    python3 test_http_server.py [path/to/http_server.py]

Runs the real server on a loopback port against a temp dir and POSTs to it; no mocks.

The property under test is CONSERVATION OF ROWS, not "does it rotate". A rotation scheme
that reuses one suffix and overwrites it passes every does-it-rotate check while
silently destroying data -- which is exactly what happened here, costing ~2/3 of three
months of meter logs. So every case counts the rows that went in against the rows on
disk afterwards.
"""
import http.client
import importlib.util
import shutil
import sys
import tempfile
import threading
from http.server import ThreadingHTTPServer
from pathlib import Path

SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("http_server.py")
spec = importlib.util.spec_from_file_location("hs", SRC)
hs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hs)

tmp = Path(tempfile.mkdtemp(prefix="hs_test_"))
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'}  {name}{'  ' + detail if detail else ''}")
    if not ok:
        failures.append(name)


def serve(handler_cls):
    srv = ThreadingHTTPServer(("127.0.0.1", 0), handler_cls)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def post(port, body):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request("POST", "/", body=body.encode())
    r = c.getresponse()
    r.read()
    c.close()
    return r.status


# ---------------------------------------------------------------- rotation
print("\n[1] rotation conserves every row")
hs.ESPDataHandler.log_dir = str(tmp)
hs.ESPDataHandler.max_log_bytes = 2000      # ~30 rows per chunk -> many rotations
hs.ESPDataHandler.max_rows_per_min = 0      # rate limit off for this test
hs.ESPDataHandler.dir_quota_bytes = 0
hs.ESPDataHandler.alert_email = None
srv, port = serve(hs.ESPDataHandler)

N = 500
codes = {post(port, f"test.csv,{i},22.5,-3.25") for i in range(N)}
srv.shutdown()

chunks = sorted(p for p in tmp.iterdir() if p.name.startswith("test.csv"))
rows = []
for p in chunks:
    rows += [l for l in p.read_text().splitlines() if l.strip()]

check("all POSTs accepted", codes == {200}, f"codes={sorted(codes)}")
check("rotated more than once", len(chunks) > 2, f"{len(chunks)} chunks")
check("no row lost", len(rows) == N, f"posted {N}, on disk {len(rows)}")
seq = sorted(int(l.split(",")[1]) for l in rows)
check("every sequence number present once", seq == list(range(N)))
check("chunk names sort chronologically",
      [p.name for p in chunks] == sorted(p.name for p in chunks),
      f"e.g. {chunks[1].name if len(chunks) > 1 else '-'}")
check("bare .csv sorts last (newest)", chunks[0].name == "test.csv")

# ---------------------------------------------------------------- rate limit
print("\n[2] rate limit drops a runaway and reports it")
hs.ESPDataHandler.max_log_bytes = 10 * 1024 * 1024
hs.ESPDataHandler.max_rows_per_min = 5
srv, port = serve(hs.ESPDataHandler)

results = [post(port, f"runaway.csv,{i}") for i in range(20)]
srv.shutdown()

accepted = results.count(200)
limited = results.count(429)
kept = len([l for l in (tmp / "runaway.csv").read_text().splitlines() if l.strip()])
check("first rows accepted", accepted == 5, f"{accepted} accepted")
check("excess rejected with 429", limited == 15, f"{limited} rejected")
check("only accepted rows written", kept == 5, f"{kept} on disk")

# ---------------------------------------------------------------- overwrite guard
print("\n[3] a same-second second rotation does not overwrite")
from datetime import datetime
p = tmp / "collide.csv"
p.write_text("first\n")
when = datetime(2026, 9, 11, 1, 45, 0)
t1 = hs.rotate_log(p, when)
p.write_text("second\n")
t2 = hs.rotate_log(p, when)
check("two rotations, two distinct files", t1 != t2, f"{t1.name} vs {t2.name}")
check("first chunk intact", t1.read_text() == "first\n")
check("second chunk intact", t2.read_text() == "second\n")

# ---------------------------------------------------------------- rotation off
print("\n[4] max_log_bytes = 0 means never rotate, not rotate every row")
hs.ESPDataHandler.max_log_bytes = 0
hs.ESPDataHandler.max_rows_per_min = 0
srv, port = serve(hs.ESPDataHandler)

M = 200
for i in range(M):
    post(port, f"plain.csv,{i},22.5")
srv.shutdown()

produced = sorted(p.name for p in tmp.iterdir() if p.name.startswith("plain.csv"))
kept = len([l for l in (tmp / "plain.csv").read_text().splitlines() if l.strip()])
check("exactly one file", produced == ["plain.csv"], f"{len(produced)} file(s)")
check("all rows in it", kept == M, f"{kept} of {M}")

# ------------------------------------------------- month-in-name exemption
print("\n[5] a month-named log is never rotated, while the cap still applies to others")
hs.ESPDataHandler.max_log_bytes = 2000          # far below what 250 rows produce
srv, port = serve(hs.ESPDataHandler)

K = 250
for i in range(K):
    post(port, f"PowerMonitor.v0.09.26.main.csv,{i},22.5,-3.25")
for i in range(K):
    post(port, f"EVR_Balance.log,{i},22.5,-3.25")
srv.shutdown()

pm = sorted(p.name for p in tmp.iterdir() if p.name.startswith("PowerMonitor"))
pm_rows = len([l for l in (tmp / "PowerMonitor.v0.09.26.main.csv").read_text().splitlines() if l.strip()])
evr = sorted(p.name for p in tmp.iterdir() if p.name.startswith("EVR_Balance.log"))
evr_rows = sum(len([l for l in (tmp / n).read_text().splitlines() if l.strip()]) for n in evr)

check("month-named log stayed one file", pm == ["PowerMonitor.v0.09.26.main.csv"], f"{len(pm)} file(s)")
check("month-named log kept every row", pm_rows == K, f"{pm_rows} of {K}")
check("non-exempt log still rotated", len(evr) > 2, f"{len(evr)} chunks")
check("non-exempt log lost nothing", evr_rows == K, f"{evr_rows} of {K}")

print("\n[6] the exemption regex against the sink's real filenames")
exempt = ["PowerMonitor.v0.09.26.main.csv", "PowerMonitor.v2.01.20.sub.csv",
          "PowerMonitor.v0.12.18.main.csv"]
plain = ["EVR_Balance.log", "OldEVR.log", "EVR_Balance.debug.log", "IrrCntrl.csv",
         "Debug_log.csv", "SDP810.csv", "Soil32.csv", "PowerMon1.csv"]
for n in exempt:
    check(f"exempt: {n}", bool(hs.MONTHLY_NAME_RE.search(n)))
for n in plain:
    check(f"not exempt: {n}", not hs.MONTHLY_NAME_RE.search(n))

# ---------------------------------------------------------------- image upload
print("\n[7] POST /image/<device> stores each body verbatim, never overwrites, rejects junk")


def post_image(port, device, body):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request("POST", f"/image/{device}", body=body, headers={"Content-Type": "image/jpeg"})
    r = c.getresponse()
    r.read()
    c.close()
    return r.status


hs.ESPDataHandler.image_dir = str(tmp / "images")
hs.ESPDataHandler.max_rows_per_min = 0
hs.ESPDataHandler.max_image_bytes = 100 * 1024
srv, port = serve(hs.ESPDataHandler)

jpeg = bytes([0xFF, 0xD8, 0xFF, 0xE0]) + bytes(range(256)) * 40 + bytes([0xFF, 0xD9])  # binary, not UTF-8
codes = {post_image(port, "SleepCam-31E494", jpeg + bytes([i])) for i in range(3)}  # same second
bad_name = post_image(port, "..%2Fetc", jpeg)
raw_traversal = post_image(port, "../../etc", jpeg)   # literal slashes, no encoding
too_big = post_image(port, "SleepCam-31E494", bytes(200 * 1024))
empty = post_image(port, "SleepCam-31E494", b"")
not_jpeg = post_image(port, "SleepCam-31E494", b"PK\x03\x04" + bytes(4096))
srv.shutdown()

dev_dir = tmp / "images" / "SleepCam-31E494"
files = sorted(dev_dir.iterdir()) if dev_dir.is_dir() else []
check("all three accepted", codes == {200}, f"codes={sorted(codes)}")
check("three distinct files (same-second burst)", len(files) == 3, f"{len(files)} file(s)")
check("bodies stored verbatim",
      sorted(p.read_bytes() for p in files) == sorted(jpeg + bytes([i]) for i in range(3)))
check("names are second-stamped .jpg", all(p.suffix == ".jpg" and len(p.stem) >= 19 for p in files))
check("traversal-shaped device rejected", bad_name == 400, f"code={bad_name}")
check("raw ../ path rejected", raw_traversal == 400, f"code={raw_traversal}")
check("oversize rejected", too_big == 413, f"code={too_big}")
check("empty rejected", empty == 413, f"code={empty}")
check("non-JPEG body rejected", not_jpeg == 415, f"code={not_jpeg}")
check("nothing landed outside the device dir",
      sorted(p.name for p in (tmp / "images").iterdir()) == ["SleepCam-31E494"])
check("still exactly three files", len(sorted(dev_dir.iterdir())) == 3)

hs.ESPDataHandler.image_dir = None
srv, port = serve(hs.ESPDataHandler)
disabled = post_image(port, "SleepCam-31E494", jpeg)
srv.shutdown()
check("route disabled without an image dir", disabled == 404, f"code={disabled}")

# ---------------------------------------------------------------- write failure is reported
print("\n[8] a row the sink could not write is answered 500, not 200")
blocked = tmp / "blocked.csv"
blocked.mkdir()                       # a directory where the file should be: open() raises
hs.ESPDataHandler.max_rows_per_min = 0
srv, port = serve(hs.ESPDataHandler)
code = post(port, "blocked.csv,1,2")
srv.shutdown()
check("unwritable row -> 500", code == 500, f"code={code}")

shutil.rmtree(tmp, ignore_errors=True)
print(f"\n{'ALL PASS' if not failures else 'FAILURES: ' + ', '.join(failures)}")
sys.exit(1 if failures else 0)

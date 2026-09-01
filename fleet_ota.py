#!/usr/bin/env python3
"""
Shared state for fleet OTA confirm / blacklist / revert.

Imported by http_server.py (records pulls + confirms, guards serving) and by
fleet_ota_watchdog.py (promotes confirmed images, reverts + blacklists images that
were deployed but never confirmed). Stdlib only.

Model (server-authoritative confirm-or-revert -- the ESP32 rollback philosophy, moved
to the one place that can protect the WHOLE fleet: the single distribution point).

  * A firmware file is <FWDIR>/<NAME>.bin. Its MD5 is the identity used everywhere --
    it is exactly what the device reports (ESPhttpUpdate sends x-ESP*-sketch-md5, and the
    BOOT line carries md5=ESP.getSketchMD5()), so no version-string coupling is needed.

  * last-known-good: <NAME>.bin.good is a byte copy of the last image that a device
    confirmed healthy. Revert = copy it back over <NAME>.bin.

  * confirm: a device POSTs a BOOT row carrying md5=<hex> (via Debug_log.csv). That the
    row arrived proves the image booted far enough to raise WiFi and post -> that md5 is
    good. (Same bar as the ESP32 native rollback: "reached STA/SoftAP".)

  * revert trigger: a NEW <NAME>.bin (md5 != last-good) that was actually PULLED by >=1
    device (a 200 firmware GET) and then went CONFIRM_WINDOW_S without any confirm for its
    md5 is bad -- the puller bricked/crashed/browned-out. The watchdog reverts to .good and
    blacklists the bad md5. The "was pulled" gate makes a false revert essentially
    impossible: an image no device fetched is never touched, so a good deploy waiting on
    sleeping devices is safe. That gate only holds because http_server.py counts a GET as a
    pull ONLY from a client presenting an ESP header -- a bare curl of the firmware URL used
    to start the clock and got a healthy image reverted and blacklisted.

  * blacklist: a bad md5 is refused by the server (served as 304 "no update") so a re-drop
    of the same bytes cannot re-poison the fleet.

Nothing here saves an already-bricked single-partition ESP8266 (it cannot pull) -- the win
is protecting every device that has not pulled yet, which only the server can do.
"""

import contextlib
import hashlib
import json
import os
import re
import shutil
import time
from pathlib import Path

STATE_BASENAME = ".fleet_ota_state.json"
LOCK_BASENAME = ".fleet_ota_state.lock"
GOOD_SUFFIX = ".good"          # <NAME>.bin.good = last-known-good byte copy
BAD_SUFFIX_FMT = ".bad-{ver}"  # reverted image kept aside as <NAME>.bin.bad-<ver>

# A confirmed-good image must post its BOOT line within this long of being pulled, or the
# puller is presumed dead and the image is reverted+blacklisted. Generous vs. real
# boot->WiFi->post latency (tens of seconds), tight enough to react within a couple of
# watchdog cycles.
CONFIRM_WINDOW_S = 15 * 60

# avp::DeviceName(NAME) = NAME + last 3 WiFi-MAC bytes as 6 hex chars. The Debug_log rows
# are tagged with that DeviceName; strip the deterministic suffix to recover the firmware
# NAME (== <NAME>.bin). A bare NAME (no suffix) is left unchanged.
_DEVICE_SUFFIX_RE = re.compile(r"-[0-9A-Fa-f]{6}$")
# md5 token as emitted in the BOOT line: a CSV field "md5=<32 hex>".
_MD5_TOKEN_RE = re.compile(r"^md5=([0-9a-fA-F]{32})$")


def firmware_name_from_device(device_name):
    """Recover the firmware NAME from a device's tag (drops the -XXYYZZ MAC suffix)."""
    return _DEVICE_SUFFIX_RE.sub("", device_name)


def md5_of(path):
    """Hex MD5 of a file, or None if it can't be read."""
    try:
        return hashlib.md5(Path(path).read_bytes()).hexdigest()
    except OSError:
        return None


def extract_md5_token(row_fields):
    """Return the md5 hex from a posted row's fields (a "md5=<hex>" field), or None."""
    for f in row_fields:
        m = _MD5_TOKEN_RE.match(f.strip())
        if m:
            return m.group(1).lower()
    return None


class FleetOTA:
    """
    Thread- and process-safe accessor for the OTA state file. Every public method takes
    the on-disk flock for the duration of a read-modify-write, so the multithreaded server
    and the cron watchdog never corrupt each other. All disk mutation of firmware files is
    done by the watchdog (promote/revert); the server only records events and reads the
    blacklist.

    State layout (<FWDIR>/.fleet_ota_state.json):
      { "<NAME>": {
          "good_md5":  "<hex>|null",              # matches <NAME>.bin.good on disk
          "current":   {"md5": "<hex>", "first_pull": epoch|null,
                        "pullers": {"<device>": epoch},
                        "confirmed_by": {"<device>": epoch}},
          "blacklist": {"<hex>": {"time": epoch, "reason": "..."}}
      } }
    The "current" block is scoped to one md5; when <NAME>.bin's md5 changes the watchdog
    resets it. Confirms are recorded per-md5 too (keyed under whichever md5 is current).
    """

    def __init__(self, firmware_dir):
        self.dir = Path(firmware_dir)
        self.state_path = self.dir / STATE_BASENAME
        self.lock_path = self.dir / LOCK_BASENAME

    # -- locking ---------------------------------------------------------------------

    @contextlib.contextmanager
    def _lock(self):
        """Exclusive advisory lock across the server threads and the watchdog process."""
        # Import here so a platform without fcntl (dev on Windows) can still import the
        # module for offline unit tests -- the lock degrades to a no-op there.
        try:
            import fcntl
        except ImportError:
            fcntl = None
        self.dir.mkdir(parents=True, exist_ok=True)
        fd = os.open(str(self.lock_path), os.O_CREAT | os.O_RDWR, 0o644)
        try:
            if fcntl:
                fcntl.flock(fd, fcntl.LOCK_EX)
            yield
        finally:
            if fcntl:
                with contextlib.suppress(OSError):
                    fcntl.flock(fd, fcntl.LOCK_UN)
            os.close(fd)

    # -- raw load/save (call inside _lock) -------------------------------------------

    def _load(self):
        try:
            return json.loads(self.state_path.read_text())
        except (OSError, ValueError):
            return {}

    def _save(self, st):
        tmp = self.state_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(st, indent=1, sort_keys=True))
        tmp.replace(self.state_path)  # atomic on POSIX

    @staticmethod
    def _entry(st, name):
        return st.setdefault(name, {"good_md5": None, "current": None, "blacklist": {}})

    # -- server-side event recording -------------------------------------------------

    def record_pull(self, name, md5, device, now=None):
        """An ESP fetched <NAME>.bin (200). Note it so a bad image can be attributed.

        Caller must have established that `device` is a real ESP: this starts the
        CONFIRM_WINDOW_S clock whose expiry reverts and blacklists the image.
        """
        now = now or time.time()
        with self._lock():
            st = self._load()
            e = self._entry(st, name)
            cur = e.get("current")
            if not cur or cur.get("md5") != md5:
                cur = {"md5": md5, "first_pull": None, "pullers": {}, "confirmed_by": {}}
                e["current"] = cur
            cur["pullers"][device] = now
            if cur.get("first_pull") is None:
                cur["first_pull"] = now
            self._save(st)

    def record_confirm(self, name, md5, device, now=None):
        """A device posted a BOOT line for `md5` -> that image is healthy on it."""
        now = now or time.time()
        with self._lock():
            st = self._load()
            e = self._entry(st, name)
            # Revert-protection gate: mark that THIS name's fleet speaks the confirm protocol
            # (its firmware emits md5= in the BOOT line). The watchdog refuses to revert a
            # name until this is set, so deploying the server/watchdog before the firmware has
            # rolled out can never falsely revert a good image -- protection self-activates
            # per name on its first md5 confirm.
            e["confirm_seen"] = now
            cur = e.get("current")
            if not cur or cur.get("md5") != md5:
                # Confirm for an image that isn't the recorded current one (e.g. the server
                # hasn't seen a pull yet, or it's the already-good running image). Record it
                # under its own md5 so the watchdog can still promote it.
                cur = {"md5": md5, "first_pull": None, "pullers": {}, "confirmed_by": {}}
                e["current"] = cur
            cur["confirmed_by"][device] = now
            self._save(st)

    def is_blacklisted(self, name, md5):
        """True if this md5 is a known-bad image for NAME (server should refuse to serve)."""
        with self._lock():
            st = self._load()
            return md5 in st.get(name, {}).get("blacklist", {})

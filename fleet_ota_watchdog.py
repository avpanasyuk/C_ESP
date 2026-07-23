#!/usr/bin/env python3
"""
Fleet OTA watchdog -- promote confirmed firmware, revert + blacklist bad firmware.

Runs from cron on the fleet server (bsd). Reads the state that http_server.py records
(pulls + confirms, see fleet_ota.py) and owns every mutation of the firmware files:

  * bootstrap   -- a <NAME>.bin with no <NAME>.bin.good yet is adopted as good as-is (the
                   currently-deployed image is presumed working; we never revert something
                   that predates this mechanism).
  * promote     -- <NAME>.bin whose md5 a device has confirmed becomes the new
                   <NAME>.bin.good (byte copy).
  * revert      -- <NAME>.bin whose md5 was PULLED by >=1 device but stayed unconfirmed for
                   CONFIRM_WINDOW_S is bad: copy <NAME>.bin.good back over <NAME>.bin, keep
                   the bad image aside as <NAME>.bin.bad-<n>, blacklist its md5, and email.

A revert only fires on an image a device actually fetched and then failed to confirm, so a
good deploy waiting on sleeping devices is never disturbed.

  fleet_ota_watchdog.py -F <firmware-dir> [--email addr] [--window-s N] [--dry-run] [-v]
"""

import argparse
import shutil
import sys
import time
from pathlib import Path

import fleet_ota
from fleet_ota import FleetOTA, md5_of


def send_mail(recipient, subject, body, mail_bin="mail"):
    """Best-effort email via mail(1); never raises (the revert already happened)."""
    import subprocess
    try:
        subprocess.run([mail_bin, "-s", subject.replace("\n", " ")[:200], recipient],
                       input=body.encode(), check=True, timeout=30)
        return True
    except Exception as e:  # noqa: BLE001 -- notification must not break the sweep
        print(f"  email to {recipient} failed: {e}", file=sys.stderr)
        return False


def next_bad_path(fw_bin):
    """<NAME>.bin.bad-1, .bad-2, ... (never clobber a prior reverted image)."""
    i = 1
    while (p := Path(str(fw_bin) + f".bad-{i}")).exists():
        i += 1
    return p


def sweep(firmware_dir, email=None, window_s=fleet_ota.CONFIRM_WINDOW_S,
          mail_bin="mail", dry_run=False, verbose=False, now=None):
    """One pass over every <NAME>.bin. Returns list of (name, action) taken."""
    now = now or time.time()
    fdir = Path(firmware_dir)
    ota = FleetOTA(firmware_dir)
    actions = []

    def vlog(msg):
        if verbose:
            print(msg)

    # Manage only names the server has actually recorded activity for (a pull or confirm) --
    # NOT every *.bin on disk. This keys off <fw-dir>/.fleet_ota_state.json, so version-tagged
    # archives that also end in .bin (e.g. WaterLeak-0.4.7.bin, which no device pulls -- devices
    # fetch /firmware/WaterLeak.bin) never get bootstrapped. A name enters state on its first
    # real pull/confirm and maps to <fw-dir>/<NAME>.bin.
    with ota._lock():
        names = sorted(ota._load().keys())

    for name in names:
        fw_bin = fdir / (name + ".bin")
        if not fw_bin.is_file():
            vlog(f"[{name}] in state but no {name}.bin on disk -- skipping")
            continue
        good_bin = Path(str(fw_bin) + fleet_ota.GOOD_SUFFIX)
        cur_md5 = md5_of(fw_bin)
        if cur_md5 is None:
            continue

        # Snapshot state for this NAME (single lock hold; decisions below use it read-only,
        # and each mutation re-locks via its own FleetOTA call / atomic file op).
        with ota._lock():
            st = ota._load()
            e = st.get(name, {"good_md5": None, "current": None, "blacklist": {}})
            good_md5 = e.get("good_md5")
            cur = e.get("current") or {}
            confirm_seen = bool(e.get("confirm_seen"))

        # 1) Bootstrap: no known-good yet -> adopt whatever is deployed now.
        if good_md5 is None or not good_bin.exists():
            vlog(f"[{name}] bootstrap: adopt current image {cur_md5[:8]} as good")
            if not dry_run:
                shutil.copy2(fw_bin, good_bin)
                _set_good(ota, name, cur_md5, now)
            actions.append((name, "bootstrap"))
            continue

        # 2) Stable: deployed image already is the known-good -> nothing to do.
        if cur_md5 == good_md5:
            continue

        # 3) A new image is deployed (md5 != good). Decide promote / revert / wait.
        confirmed = bool(cur.get("md5") == cur_md5 and cur.get("confirmed_by"))
        pulled_at = cur.get("first_pull") if cur.get("md5") == cur_md5 else None

        if confirmed:
            vlog(f"[{name}] promote: {cur_md5[:8]} confirmed by "
                 f"{list(cur['confirmed_by'])}")
            if not dry_run:
                shutil.copy2(fw_bin, good_bin)
                _set_good(ota, name, cur_md5, now)
            actions.append((name, "promote"))
            continue

        if pulled_at is not None and (now - pulled_at) > window_s and not confirm_seen:
            # Deployed and pulled, but this name has NEVER produced a confirm -> its firmware
            # doesn't emit md5= yet (pre-rollout). Do not revert: a missing confirm here means
            # "protocol not deployed", not "image bad". Self-heals once firmware rolls out.
            vlog(f"[{name}] revert-protection INACTIVE (no md5 confirm ever seen); leaving "
                 f"{cur_md5[:8]} in place")
            actions.append((name, "revert-protection-inactive"))
            continue

        if pulled_at is not None and (now - pulled_at) > window_s and confirm_seen:
            # Deployed, fetched by a device, no confirm within the window -> bad.
            age = int(now - pulled_at)
            pullers = list(cur.get("pullers", {}))
            vlog(f"[{name}] REVERT: {cur_md5[:8]} pulled {age}s ago by {pullers}, "
                 f"never confirmed -> restore good {good_md5[:8]}")
            if not dry_run:
                bad_path = next_bad_path(fw_bin)
                shutil.move(str(fw_bin), str(bad_path))      # keep the bad image aside
                shutil.copy2(good_bin, fw_bin)               # restore known-good
                _blacklist(ota, name, cur_md5, now,
                           f"pulled by {pullers}, no confirm in {age}s")
                if email:
                    send_mail(email, f"[fleet-ota] reverted {name}",
                              f"{name}.bin (md5 {cur_md5}) was pulled by {pullers} but never "
                              f"posted a BOOT confirm within {age}s.\n"
                              f"Reverted to last-good (md5 {good_md5}); bad image kept as "
                              f"{bad_path.name}; md5 blacklisted.\n", mail_bin=mail_bin)
            actions.append((name, "revert"))
            continue

        # 4) New image, not yet confirmed and either not pulled or still inside the window.
        if pulled_at is None:
            vlog(f"[{name}] waiting: new image {cur_md5[:8]} not yet pulled by any device")
        else:
            vlog(f"[{name}] waiting: {cur_md5[:8]} pulled "
                 f"{int(now - pulled_at)}s ago, within {window_s}s confirm window")

    return actions


def _set_good(ota, name, md5, now):
    """Mark `md5` as the known-good for NAME and clear its probation record."""
    with ota._lock():
        st = ota._load()
        e = FleetOTA._entry(st, name)
        e["good_md5"] = md5
        # Once good, the probation block for this md5 is spent; drop it so a later re-deploy
        # of a different md5 starts a fresh probation.
        if (e.get("current") or {}).get("md5") == md5:
            e["current"] = None
        ota._save(st)


def _blacklist(ota, name, md5, now, reason):
    with ota._lock():
        st = ota._load()
        e = FleetOTA._entry(st, name)
        e.setdefault("blacklist", {})[md5] = {"time": now, "reason": reason}
        if (e.get("current") or {}).get("md5") == md5:
            e["current"] = None
        ota._save(st)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-F", "--firmware-dir", required=True)
    ap.add_argument("--email", default=None, help="notify this address on a revert")
    ap.add_argument("--mail-bin", default="mail")
    ap.add_argument("--window-s", type=int, default=fleet_ota.CONFIRM_WINDOW_S,
                    help=f"confirm window seconds (default {fleet_ota.CONFIRM_WINDOW_S})")
    ap.add_argument("--dry-run", action="store_true", help="decide but change nothing")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    actions = sweep(args.firmware_dir, email=args.email, window_s=args.window_s,
                    mail_bin=args.mail_bin, dry_run=args.dry_run, verbose=args.verbose)
    if actions:
        tag = "[dry-run] " if args.dry_run else ""
        for name, act in actions:
            print(f"{tag}{name}: {act}")
    elif args.verbose:
        print("no actions")


if __name__ == "__main__":
    main()

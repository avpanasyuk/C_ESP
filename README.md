# C_ESP

ESP8266 / ESP32 networking, web-server, logging and GPIO helpers for Arduino
projects. Depends on its sibling library [`C_General`](../C_General) (included via
relative paths like `"../C_General/MyTime.hpp"`) and on
[`C_ARDUINO`](../C_ARDUINO) (`#include "C_ARDUINO/General.h"`). All three are
normally vendored side-by-side under a project's `src/` so the implicit `-Isrc`
resolves the cross-includes.

## Headers

| Header | What it provides |
|--------|------------------|
| `StaticWebServer.hpp` | `avp::StaticWebServer` — fully static wrapper over `(ESP8266)WebServer`, mounts `HTTPUpdateServer` at `/update`, inherits `StaticWiFi_Conn`. Built-in routes `/`, `/config` (set WiFi creds → LittleFS), `/forget` (erase stored creds, reboot to the build-time default), `/pin`, `/log`, `/reset`, `/update`. Configure with `Options_t`; register routes via `on(uri, handler[, method])`; drive with `call_in_loop()`. |
| `StaticWiFi_Conn.hpp` | `avp::StaticWiFi_Conn` — WiFi state machine (`IDLE / TRYING_TO_CONNECT / AP_MODE / SCANNING / CONNECTED`), per-device credentials in LittleFS at `/net_auth.txt` (`StoreAUTH`/`ForgetAUTH`), softAP fallback, periodic rescan + roam to a stronger same-SSID AP. Blinks `LED_pin` per state from an `HW_Timer_ms` ISR. Optional `ArduinoOTA` when `DO_OTA` is set. **No credentials in source:** the fallback default comes from `WIFI_DEFAULT_SSID`/`WIFI_DEFAULT_PASS` build-flag macros (empty unless the consumer injects them, e.g. from a gitignored `secrets.ini`); an empty default makes an unprovisioned device open its `/config` softAP. The stored SSID is logged but the password never is. **OTA rollback (ESP32 only):** a freshly-OTA'd image boots on probation (`PENDING_VERIFY`); it is confirmed valid once it reaches STA *or* a working SoftAP (both let you reach `/config`), and the bootloader auto-reverts to the previous image if it reaches neither (crash/hang/bootloop) — so a bad OTA self-recovers without a USB trip. Needs `DO_OTA`, the `verifyRollbackLater()→true` override in `common_esp.cpp` (linked automatically), and a dual-OTA partition table (e.g. `min_spiffs`). No-op on ESP8266 (eboot copies the new image down over the old one — no slot to revert to; SoftAP fallback remains its safety net). |
| `HTML_Log.hpp` | `avp::HTML_Log` static log buffer surfaced at `/log`. `begin(doTimeMarks, size, break)`, `Add()`, `AddLine()`, `Get()`. `StaticWebServer::begin()` calls it automatically. |
| `RemoteLog.hpp` | `avp::RemoteLog<MaxLineBytes>` — CSV-framed POST logger (`"<filename>,<csv>"`) paired with `http_server.py`. Single static buffer, main-loop only. Needs `client.cpp` linked. |
| `FleetServerOTA.hpp` | `avp::PullUpdateFromFleetServer(name, version, server="bsd", port=8000)` — HTTP-pull firmware update: GETs `http://<server>:<port>/firmware/<name>.bin` and flashes it if the image differs (MD5-gated by the server, via the framework's `ESPhttpUpdate`/`httpUpdate`). Reboots on update; safe to call every cycle. Pairs with `http_server.py`'s `/firmware/` endpoint. ESP8266 + ESP32. |
| `FleetServerDebug.hpp` | `avp::FleetServerDebug` — tee `debug_puts()` output to a shared `Debug_log.csv` on the fleet server, tagged with the device name (POSTs `"<file>,<name>,<line>"`). Line-buffers (one row per `\n`), re-entrancy-guarded (the recursion via HTTP_POST_puts errors), posts only when WiFi is up. Main-loop only; needs `client.cpp` linked. |
| `client.hpp` / `client.cpp` | `avp::Client` / `avp::Client_Secure` HTTP client (framework `ESP8266HTTPClient`); also provides `HTTP_POST_puts` used by `RemoteLog`. |
| `service.h` | `avp::GenerateHTML`, `avp::scan` (HTML AP table), `avp::FindTheBestAPinScan`, `avp::FindBestAP`, `avp::DeviceName(prefix)` (cached `"<prefix>-XXYYZZ"` from the last 3 MAC bytes, for hostname/OTA/log identity), `avp::HTTP_POST_puts`, `BSSIDtoString`; `PAUSE_ESP_INTERRUPTS` macro. |
| `fast_gpio.hpp` | `avp::SetPin / ClearPin / TogglePin` register-level GPIO (ISR-safe). |
| `hw_timer.hpp` | `avp::HW_Timer_ms<>::CreateTimer(fn, ms, autoreload)`. |
| `fast_wake.hpp`, `pcnt_ll.h` | Fast-wake and pulse-counter low-level helpers. `Read`/`Write`/`SnapshotFastWakeWiFi` cache the BSSID/channel/IP in RTC memory so a deep-sleep wake skips scan+DHCP; `InvalidateFastWakeWiFi()` forces the next wake back onto the slow path — call it when the cached association comes up but the network turns out unreachable (a reassigned DHCP lease), which nothing else detects. |
| `common_esp.cpp` | Out-of-line implementations for `service.h`. |
| `http_server.py` | Companion server: writes each POSTed `<filename>,<csv>` body to `<log-dir>/<filename>` with a timestamp column prepended, **and** serves firmware at `/firmware/<name>.bin` for `FleetServerOTA.hpp` (MD5-gated: 304 when the device already runs the image). **A log whose filename carries the month is never size-rotated** (`--no-rotate-pattern`, matching e.g. `PowerMonitor.v0.09.26.main.csv`): the name already gives it a natural monthly boundary, and splitting it would hand every reader a chance to see part of a month and believe it saw all of it. Other names still rotate past `--max-log-bytes` (default 10 MiB), but **nothing is ever deleted** — the file is renamed aside as `<file>.<YYYYmmdd-HHMMSS>` and kept, so a series is `<file>` plus its timestamped siblings, which sort chronologically as plain strings. Disk protection against a crash-looping device is `--max-rows-per-min` (default 120, per filename): excess rows get a 429 and one alert to `--alert-email`, a rate a device on a sane cadence can never reach. `--dir-quota-bytes` alerts if the whole log dir passes a threshold. Neither deletes. Also records fleet-OTA pulls/confirms and refuses to serve a blacklisted MD5 (see `fleet_ota.py`). |
| `fleet_ota.py` | Server-authoritative OTA confirm/blacklist state shared by `http_server.py` and `fleet_ota_watchdog.py`. Identity is the image MD5 (what the device already reports). `<NAME>.bin.good` is the last image a device confirmed healthy. A device confirms an image by posting a `BootLog` BOOT row carrying `md5=<sketch-md5>` — proof it booted far enough to raise WiFi. A freshly OTA'd image always boots with reason `Software/System restart`, so a battery device may pass `LogBoot(..., with_md5=false)` on its routine timer/event wakes (the md5 costs a full-image flash read) and still confirm every image it pulls. Stdlib only; flock-guarded state file `<fw-dir>/.fleet_ota_state.json`. |
| `fleet_ota_watchdog.py` | Cron sweep (owns all firmware-file mutation): bootstraps an unknown `<NAME>.bin` as good, promotes a confirmed image to `<NAME>.bin.good`, and — only for a name whose fleet has ever emitted an `md5=` confirm — reverts + blacklists a `<NAME>.bin` that was pulled by a device but stayed unconfirmed past `--window-s`. The per-name confirm gate makes it safe to enable before the `md5=` firmware finishes rolling out. `-F <fw-dir> [--email a] [--dry-run] [-v]`. |

## Build requirements it imposes on the consumer

- `board_build.filesystem = littlefs` — WiFi credentials live in LittleFS.
- **`WIFI_DEFAULT_SSID` / `WIFI_DEFAULT_PASS` — inject these, or risk unreachable
  devices.** They are the compile-time WiFi fallback used whenever LittleFS
  `/net_auth.txt` holds no valid creds (a fresh unit, after `/forget`, or a unit
  that was only ever connecting via the *previous* firmware's baked-in default).
  Both default to `""`, and **an empty default makes such a device come up in
  `/config`-SoftAP-only mode after a (re)flash — unreachable over the LAN, which
  is fatal for a physically inaccessible module.** Provisioning a unit once via
  `/config` (which writes LittleFS) is *not* sufficient safety on its own; always
  bake a real fallback too. Inject it from a **gitignored `secrets.ini`** so the
  password never enters git:

  ```ini
  ; platformio.ini
  [platformio]
  extra_configs = secrets.ini
  [common]                     ; (or [env] — wherever build_flags live)
  build_flags = ... -DWIFI_DEFAULT_SSID=\"${secrets.wifi_ssid}\"
                    -DWIFI_DEFAULT_PASS=\"${secrets.wifi_pass}\"

  ; secrets.ini  (gitignored; commit a secrets.ini.example placeholder instead)
  [secrets]
  wifi_ssid = YourSSID
  wifi_pass = YourPassword
  ```
  Add `secrets.ini` to `.gitignore`. Leaving the default **blank is correct only**
  when every unit is provisioned by hand through its `/config` SoftAP and none is
  ever out of physical reach.
- `-DNAME=\"YourName\"` — used for hostname / mDNS and the default device name.
- `-DDO_OTA=1` — required for the ArduinoOTA (`espota`) upload path.
- `build_src_filter` must list the `.cpp`/`.c` TUs you actually use, e.g.
  `+<C_ESP/common_esp.cpp>` and exactly one of `client.cpp` (HTTPClient) — do not
  also pull a conflicting `avp::Client_` implementation.

## Gotchas

- `LED_BUILTIN` is owned by the WiFi blink. Set `LED_pin = 0xFF` or pass a custom
  `status_indication_func_` to reuse it.
- A function passed to `HW_Timer` must **not** be a template-class static member
  marked `IRAM_ATTR` — GCC doesn't reliably place template instantiations in IRAM
  and it crashes on the first ISR fire. Use a plain non-template function.
- OTA callbacks (`onStart/onEnd/onError`) must not read flash strings (`F("...")`,
  `PSTR`, `__PRETTY_FUNCTION__`) — IROM may fault mid-update.
- Recurring `Found …` / `Connected in STA mode` lines in `/log` are normal
  background AP roaming, not WiFi instability.

## Repo notes

Canonical: `github.com/avpanasyuk/C_ESP`; trunk is `development`. The `HOME` mirror
is a non-bare working clone on `bsd` (`ssh://bsd/~panasyuk/GIT_REPS/LIBS/C/ESP`),
checked out on `development`, that also serves the `http_server` files — a push to
`development` updates the served checkout (`receive.denyCurrentBranch=updateInstead`).
Don't edit files directly in that clone.

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
| `StaticWiFi_Conn.hpp` | `avp::StaticWiFi_Conn` — WiFi state machine (`IDLE / TRYING_TO_CONNECT / AP_MODE / SCANNING / CONNECTED`), per-device credentials in LittleFS at `/net_auth.txt` (`StoreAUTH`/`ForgetAUTH`), softAP fallback, periodic rescan + roam to a stronger same-SSID AP. Blinks `LED_pin` per state from an `HW_Timer_ms` ISR. Optional `ArduinoOTA` when `DO_OTA` is set. **No credentials in source:** the fallback default comes from `WIFI_DEFAULT_SSID`/`WIFI_DEFAULT_PASS` build-flag macros (empty unless the consumer injects them, e.g. from a gitignored `secrets.ini`); an empty default makes an unprovisioned device open its `/config` softAP. The stored SSID is logged but the password never is. |
| `HTML_Log.hpp` | `avp::HTML_Log` static log buffer surfaced at `/log`. `begin(doTimeMarks, size, break)`, `Add()`, `AddLine()`, `Get()`. `StaticWebServer::begin()` calls it automatically. |
| `RemoteLog.hpp` | `avp::RemoteLog<MaxLineBytes>` — CSV-framed POST logger (`"<filename>,<csv>"`) paired with `http_server.py`. Single static buffer, main-loop only. Needs `client.cpp` linked. |
| `FleetServerOTA.hpp` | `avp::PullUpdateFromFleetServer(name, version, server="bsd", port=8000)` — HTTP-pull firmware update: GETs `http://<server>:<port>/firmware/<name>.bin` and flashes it if the image differs (MD5-gated by the server, via the framework's `ESPhttpUpdate`/`httpUpdate`). Reboots on update; safe to call every cycle. Pairs with `http_server.py`'s `/firmware/` endpoint. ESP8266 + ESP32. |
| `FleetServerDebug.hpp` | `avp::FleetServerDebug` — tee `debug_puts()` output to a shared `Debug_log.csv` on the fleet server, tagged with the device name (POSTs `"<file>,<name>,<line>"`). Line-buffers (one row per `\n`), re-entrancy-guarded (the recursion via HTTP_POST_puts errors), posts only when WiFi is up. Main-loop only; needs `client.cpp` linked. |
| `client.hpp` / `client.cpp` | `avp::Client` / `avp::Client_Secure` HTTP client (framework `ESP8266HTTPClient`); also provides `HTTP_POST_puts` used by `RemoteLog`. |
| `service.h` | `avp::GenerateHTML`, `avp::scan` (HTML AP table), `avp::FindTheBestAPinScan`, `avp::FindBestAP`, `avp::DeviceName(prefix)` (cached `"<prefix>-XXYYZZ"` from the last 3 MAC bytes, for hostname/OTA/log identity), `avp::HTTP_POST_puts`, `BSSIDtoString`; `PAUSE_ESP_INTERRUPTS` macro. |
| `fast_gpio.hpp` | `avp::SetPin / ClearPin / TogglePin` register-level GPIO (ISR-safe). |
| `hw_timer.hpp` | `avp::HW_Timer_ms<>::CreateTimer(fn, ms, autoreload)`. |
| `fast_wake.hpp`, `pcnt_ll.h` | Fast-wake and pulse-counter low-level helpers. |
| `common_esp.cpp` | Out-of-line implementations for `service.h`. |
| `http_server.py` | Companion server: writes each POSTed `<filename>,<csv>` body to `<log-dir>/<filename>` with a timestamp column prepended, **and** serves firmware at `/firmware/<name>.bin` for `FleetServerOTA.hpp` (MD5-gated: 304 when the device already runs the image). Rotates a log to `<file>.1` once it passes `--max-log-bytes` (default 10 MiB) so a chatty device can't fill the disk. |

## Build requirements it imposes on the consumer

- `board_build.filesystem = littlefs` — WiFi credentials live in LittleFS.
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

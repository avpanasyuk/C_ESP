/**
 * @brief Device-side firmware update by HTTP pull from the fleet server.
 *
 * Pairs with http_server.py (in this repo): the device GETs
 *   http://<server>:<port>/firmware/<name>.bin
 * and the server flashes it only when the running image differs. The library
 * sends x-ESP8266-sketch-md5; http_server.py returns 304 on a match and
 * 200 + image otherwise, so PullUpdateFromFleetServer() is safe to call every
 * loop / cycle -- no reboot loop. On a successful update the chip reboots and
 * the call does not return.
 *
 * Drop a new <name>.bin in the server's firmware dir and every polling device
 * picks it up. (The fleet server host is a parameter -- default "bsd" -- so it
 * is not baked into the name; pass another host if it moves.)
 */
#pragma once
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPUpdate.h>
#endif

#include "C_General/Error.hpp" // debug_printf / debug_puts (overridable)

namespace avp {
  /**
   * @brief Pull "<name>.bin" from http://<server>:<port>/firmware/ and flash it
   *        if the server's image differs from the one running. On success logs
   *        FW_UPDATE,was=<version> (via debug_printf, so it reaches whatever sink
   *        the project wired -- e.g. the fleet Debug_log) and reboots into the new
   *        image (does not return); otherwise returns the result.
   *
   * @param name    firmware base name -- the file pulled is "<name>.bin"
   *                (usually the NAME macro / device netname).
   * @param version current firmware version string, sent as x-ESP8266-version
   *                (logged by the server; the actual update decision is by MD5).
   * @param server  fleet-server host. Default "bsd".
   * @param port    http_server.py port. Default 8000.
   * @retval HTTPUpdateResult HTTP_UPDATE_NO_UPDATES / HTTP_UPDATE_FAILED
   *         (HTTP_UPDATE_OK never seen by the caller -- the chip reboots first).
   */
  inline HTTPUpdateResult PullUpdateFromFleetServer(const char *name, const char *version,
                                                    const char *server = "bsd", uint16_t port = 8000) {
    WiFiClient client;
    String url = String("http://") + server + ":" + port + "/firmware/" + name + ".bin";

    // Take over the reboot (rebootOnUpdate(false)) so a successful update can be
    // logged before we restart into the new image -- otherwise the library reboots
    // from inside update() and never returns, leaving no chance to log. Logging
    // here is safe: the flash write is finished and WiFi is still up, unlike a log
    // from inside an OTA progress callback mid-write. Only the update()/
    // rebootOnUpdate calls + error string are platform-specific; result handling
    // below stays common so no if/else straddles the #if branches.
#if defined(ESP8266)
    ESPhttpUpdate.rebootOnUpdate(false);
    HTTPUpdateResult r = ESPhttpUpdate.update(client, url, version);
    String err = (r == HTTP_UPDATE_FAILED) ? ESPhttpUpdate.getLastErrorString() : String();
#elif defined(ESP32)
    httpUpdate.rebootOnUpdate(false);
    HTTPUpdateResult r = httpUpdate.update(client, url, version);
    String err = (r == HTTP_UPDATE_FAILED) ? httpUpdate.getLastErrorString() : String();
#else
#error "avp::PullUpdateFromFleetServer requires ESP8266 or ESP32"
#endif

    if(r == HTTP_UPDATE_OK) {
      // A new image was flashed. Log it (the new firmware's boot line reports the
      // new version; this records the one being replaced), then reboot into it.
      debug_printf("FW_UPDATE,was=%s\n", version);
      ESP.restart(); // boot the freshly written image; does not return
    }
    // Quiet on the common no-update case (this may be polled often); only
    // failures are logged, and the result is returned for the caller to act on.
    if(r == HTTP_UPDATE_FAILED) debug_printf("FleetServer OTA failed: %s\n", err.c_str());
    return r;
  } // PullUpdateFromFleetServer
} // namespace avp

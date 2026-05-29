#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#endif

#include "client.hpp"
#include "C_General/General.h"

namespace avp {
  /**
   * POST `s` (`sz` bytes) to `URL` with Content-Type text/plain.
   * Returns nullptr on success (HTTP 200), or a static error string describing
   * the failure (URL, WiFi state, HTTPClient errno+text, free heap) so that
   * callers logging to a remote/HTML log buffer get something actionable.
   *
   * Each call opens a fresh TCP connection (setReuse defaults to false). An
   * earlier version called setReuse(true) "to save reconnection cost", but
   * Python http.server (the typical bsd-side receiver) closes the connection
   * after the response, so every call after the first returned
   * HTTPC_ERROR_CONNECTION_FAILED (-1) -- HTTPClient kept trying to use the
   * dead socket. Fresh-connect-each-call is reliable for these low-rate POSTs.
   */
  const char *HTTP_POST_puts(const char *URL, const char *s, size_t sz) {
    if(WiFi.status() != WL_CONNECTED) {
      return sprintf_static("POST %s: WiFi not connected (status=%d)", URL, (int)WiFi.status());
    }

    HTTPClient http;
#ifdef ESP32
    if(!http.begin(URL)) {
      return sprintf_static("POST %s: http.begin() failed", URL);
    }
    http.addHeader("Content-Type", "text/plain");
#endif
#ifdef ESP8266
    WiFiClient client;
    if(!http.begin(client, URL)) {
      return sprintf_static("POST %s: http.begin() failed (URL parse/connection setup)", URL);
    }
    http.addHeader("Content-Type", "text/plain");
#endif

    // Fail fast: this POST is synchronous, so a stalled connection blocks the
    // caller's main loop (and anything it does between posts, e.g. sampling).
    // 1 s is ample on a healthy LAN; the HTTPClient default is 5 s.
    http.setTimeout(1000);
    int httpCode = http.POST((uint8_t *)s, sz);
    http.end();

    if(httpCode == 200) return nullptr;

    // Non-200: build an informative error. errorToString returns a String;
    // store it in a named local so its buffer outlives sprintf_static's read.
    String errText = (httpCode < 0) ? HTTPClient::errorToString(httpCode) : String("HTTP status");
    return sprintf_static("POST %s: code=%d (%s), free heap=%lu",
                          URL, httpCode, errText.c_str(), (unsigned long)ESP.getFreeHeap());
  } // HTTP_POST_puts

  const char *HTTP_POST_puts(const char *URL, const char *s) {
    return HTTP_POST_puts(URL, s, strlen(s));
  } // HTTP_POST_puts
} // namespace avp

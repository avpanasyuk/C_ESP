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
#include "C_General/General.hpp"
#include <stdarg.h>

namespace avp {
  static void error_sink(const char *s) { // see HTTP_POST_error_sink (client.hpp)
    if(HTTP_POST_error_sink) HTTP_POST_error_sink(s);
    else Serial.print(s);
  }
  static void error_vprintf(const char *fmt, va_list ap) { svprintf_puts(error_sink, fmt, ap); }
  PRINTF_WRAPPER_VOID(log_error, error_vprintf)

  /**
   * POST `s` (`sz` bytes) to `URL` with Content-Type text/plain.
   * Returns true on success (HTTP 200). On failure it returns false and reports
   * the cause (URL, WiFi state, HTTPClient errno+text, free heap) itself via
   * log_error() -- callers must NOT re-log the failure through debug_puts (that
   * is what HTTP_POST_puts implements, so it would recurse).
   *
   * Each call opens a fresh TCP connection (setReuse defaults to false). An
   * earlier version called setReuse(true) "to save reconnection cost", but
   * Python http.server (the typical bsd-side receiver) closes the connection
   * after the response, so every call after the first returned
   * HTTPC_ERROR_CONNECTION_FAILED (-1) -- HTTPClient kept trying to use the
   * dead socket. Fresh-connect-each-call is reliable for these low-rate POSTs.
   */
  bool HTTP_POST_puts(const char *URL, const char *s, size_t sz) {
    if(WiFi.status() != WL_CONNECTED) {
      log_error("POST %s: WiFi not connected (status=%d)\n", URL, (int)WiFi.status());
      return false;
    }

    HTTPClient http;
#ifdef ESP32
    if(!http.begin(URL)) {
      log_error("POST %s: http.begin() failed\n", URL);
      return false;
    }
    http.addHeader("Content-Type", "text/plain");
#endif
#ifdef ESP8266
    WiFiClient client;
    if(!http.begin(client, URL)) {
      log_error("POST %s: http.begin() failed (URL parse/connection setup)\n", URL);
      return false;
    }
    http.addHeader("Content-Type", "text/plain");
#endif

    // Fail fast: this POST is synchronous, so a stalled connection blocks the
    // caller's main loop (and anything it does between posts, e.g. sampling).
    // 1 s is ample on a healthy LAN; the HTTPClient default is 5 s.
    http.setTimeout(1000);
    int httpCode = http.POST((uint8_t *)s, sz);
    http.end();

    if(httpCode == 200) return true;

    String errText = (httpCode < 0) ? HTTPClient::errorToString(httpCode) : String("HTTP status");
    log_error("POST %s: code=%d (%s), free heap=%lu\n",
               URL, httpCode, errText.c_str(), (unsigned long)ESP.getFreeHeap());
    return false;
  } // HTTP_POST_puts

  bool HTTP_POST_puts(const char *URL, const char *s) {
    return HTTP_POST_puts(URL, s, strlen(s));
  } // HTTP_POST_puts
} // namespace avp

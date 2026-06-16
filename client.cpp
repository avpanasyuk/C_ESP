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
#include "HTML_Log.hpp"
#include "C_General/General.hpp"
#include <stdarg.h>

namespace avp {
  // Log HTTP_POST_puts's own diagnostics into the local /log (HTML_Log) buffer.
  // Deliberately NOT via debug_puts and NOT by POSTing: HTTP_POST_puts is the
  // transport behind the FleetServerDebug debug_puts tee, so routing its errors
  // through debug_puts (or POSTing them) would recurse into HTTP_POST_puts and,
  // via the single shared sprintf_static buffer, clobber the very line being
  // shipped -- and there is no point POSTing an error about a POST that just
  // failed. The local format buffer keeps this off the shared sprintf_static
  // buffer; HTML_Log::Add only appends to its own buffer.

  PRINTF_WRAPPER_VOID(log_error, HTML_Log::vprintf);

  /*
  static void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
  static void log_error(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    HTML_Log::Add(buf);
  }
  */

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

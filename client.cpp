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
   * NOTE: the underlying HTTPClient is `static` with setReuse(true) so the TCP
   * connection is held open across calls. WiFiClient is also held in a static
   * here (was previously stack-allocated, which broke setReuse: the underlying
   * socket died when the stack WiFiClient destructed, so every call after the
   * first got HTTPC_ERROR_CONNECTION_FAILED [-1]).
   */
  const char *HTTP_POST_puts(const char *URL, const char *s, size_t sz) {
    static HTTPClient http;
#ifdef ESP8266
    static WiFiClient client; // must outlive setReuse'd http
#endif
    static const char *OldURL{nullptr};

    if(URL != OldURL) { // running once
      if(OldURL != nullptr) return "Cannot change URL in HTTP_POST_puts!\n";
      http.setReuse(true);
      OldURL = URL;
    }

    if(WiFi.status() != WL_CONNECTED) {
      return sprintf_static("POST %s: WiFi not connected (status=%d)", URL, (int)WiFi.status());
    }

#ifdef ESP32
    http.begin(URL);
    http.addHeader("Content-Type", "text/plain");
#endif

#ifdef ESP8266
    if(!http.begin(client, URL)) {
      return sprintf_static("POST %s: http.begin() failed (URL parse/connection setup)", URL);
    }
    http.addHeader("Content-Type", "text/plain");
#endif

    int httpCode = http.POST((uint8_t *)s, sz);
    http.end();

    if(httpCode == 200) return nullptr;

    // Non-200: build a maximally-informative error. HTTPClient::errorToString
    // turns negative codes (HTTPC_ERROR_CONNECTION_FAILED = -1, etc.) into
    // human-readable strings; positive codes are HTTP status (404, 500...).
    const char *codeText = (httpCode < 0)
                             ? HTTPClient::errorToString(httpCode).c_str()
                             : "(HTTP status; see code)";
    return sprintf_static("POST %s: code=%d (%s), free heap=%lu",
                          URL, httpCode, codeText, (unsigned long)ESP.getFreeHeap());
  } // HTTP_POST_puts

  const char *HTTP_POST_puts(const char *URL, const char *s) {
    return HTTP_POST_puts(URL, s, strlen(s));
  } // HTTP_POST_puts
} // namespace avp

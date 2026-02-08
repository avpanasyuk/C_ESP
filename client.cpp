#ifdef ESP8266
// #include "ESP8266WiFi.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#endif
#ifdef ESP32
// #include "WiFi.h"
#include <HTTPClient.h>
#endif

#include "client.hpp"
#include "C_General/General.h"

namespace avp {
  const char *HTTP_POST_puts(const char *URL, const char *s, size_t sz) {
    static HTTPClient http;
    static const char *OldURL{nullptr};

    if(URL != OldURL) { // running once
      if(OldURL != nullptr) return "Cannot change URL in HTTP_POST_puts!\n";
      http.setReuse(true);
      OldURL = URL;
    }

#ifdef ESP32
    http.begin(URL);
    http.addHeader("Content-Type", "text/plain");
#endif

#ifdef ESP8266
    WiFiClient client;
    if(!http.begin(client, URL)) { // new API
      // handle error
      return "begin failed";
    }
#endif

    int httpCode = http.POST((uint8_t *)s, sz);
    http.end();

    if(httpCode != 200) return sprintf_static("Wrong response code %d!", httpCode);
    return nullptr;
  } // HTTP_POST_puts

  const char *HTTP_POST_puts(const char *URL, const char *s) {
    return HTTP_POST_puts(URL, s, strlen(s));
  } // HTTP_POST_puts
} // namespace avp
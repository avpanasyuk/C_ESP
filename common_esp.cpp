#include <Arduino.h>
#ifdef ESP8266
#include "ESP8266WiFi.h"
#endif
#ifdef ESP32
#include "WiFi.h"
#endif
#include "C_General/General.hpp"
#include "C_ESP/service.h"

namespace avp {
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s, const char *title) {
    static String out;
    if(!out) out.reserve(200);
    out.clear();
    out += F("<!DOCTYPE html><html><head>");
    if(title != nullptr) {
      out += "<title>";
      out += title;
      out += "</title>";
    }
    if(AutoRefresh_s != 0) {
      out += F("<meta http-equiv=\"refresh\" content=\"");
      out += AutoRefresh_s;
      out += "\">";
    }
    out += "</head><body>";
    // out += avp::urlencode(html_body);
    out += html_body;
    out += "</body></html>";
#ifdef DEBUG
    // Serial.println(out);
#endif
    return out;
  } // GenerateHTML

  bool FindBestAP(const char *SSID, uint8_t *BSSID_out) {
    avp::ReleaseWhenOutOfScope<int> n(
#if defined(ESP8266)
      WiFi.scanNetworks(false, false, 0, (uint8_t *)SSID),
#else
      WiFi.scanNetworks(false, false, false, 300U, 0, SSID, nullptr),
#endif
      [](int) {
        WiFi.scanDelete();
      });

    int BestRSSI_i = -1;
    int32_t BestRSSI = INT32_MIN;

    for(int i = 0; i < n; ++i) {
      uint8_t *BSSID = WiFi.BSSID(i);
      debug_printf("Found %s, RSSI:%d, BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
      if(WiFi.RSSI(i) > BestRSSI) BestRSSI = WiFi.RSSI(BestRSSI_i = i);
    }
    memcpy(BSSID_out, WiFi.BSSID(BestRSSI_i), 6);

    return BestRSSI_i != -1;
  } // FindBestAP

  bool TryToConnectWiFi(const char *SSID, const char *PWD) {
    uint8_t BSSID_out[6];
    if(!FindBestAP(SSID, BSSID_out)) return false; // until there is WiFi nothing to be done
    // Keep your WiFi and OTA setup
    WiFi.mode(WIFI_STA);
#ifdef NAME
    WiFi.setHostname(NAME);
#endif
    WiFi.begin(SSID, PWD, 0, BSSID_out);
    WiFi.waitForConnectResult(20000UL);
    return WiFi.isConnected();
  }
} // namespace avp

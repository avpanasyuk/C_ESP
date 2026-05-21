#include <Arduino.h>
#ifdef ESP8266
#include "ESP8266WiFi.h"
#define WIFI_AUTH_OPEN AUTH_OPEN
#endif
#ifdef ESP32
#include "WiFi.h"
#endif
#include "C_General/General.hpp"
#include "C_General/Error.hpp"
#include "C_ESP/service.h"
#include "C_ARDUINO/General.h"

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
  
  /**
  * resets Channel if Scan fails
  */
  bool FindTheBestAPinScan(uint8_t &BestRSSI_i, int32_t &Channel) {
    auto NumAPs = WiFi.scanComplete();
    if(NumAPs < 1) {
      Channel = 0; // we can not find old channel, lets check all of them
      return false;
    }
    int32_t BestRSSI = INT32_MIN; // initial lowwest possible value
    Channel = WiFi.channel(0);    // if found channels are different then in future I scan all channels
                               // 0 indicates that channels are different

    for(int i = 0; i < NumAPs; ++i) {
      uint8_t *BSSID = WiFi.BSSID(i);
      debug_printf("Found %s, RSSI:%d, BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
        WiFi.SSID(i).c_str(), WiFi.RSSI(i), BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
      if(Channel) Channel = Channel != WiFi.channel(i) ? 0 : WiFi.channel(i);
      if(WiFi.RSSI(i) > BestRSSI) BestRSSI = WiFi.RSSI(BestRSSI_i = i);
    }
    return true;
  } // FindTheBestAPinScan

  String BSSIDtoString(const uint8_t *BSSID) {
    if(!BSSID) return "00:00:00:00:00:00";
    return String_printf("%02x:%02x:%02x:%02x:%02x:%02x",
      BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
  } // BSSIDtoString

  const String &scan() {
    static String WiFi_Around;
    WiFi_Around.reserve(512); // reserve buffer for response to avoid dynamic memory allocation
    // Scan for WiFi networks
    int n = WiFi.scanNetworks(false, false);
    WiFi_Around.clear();
    WiFi_Around += "<table><tr><th>SSID</th><th>RSSI</th><th>Protected</th><th>BSSID</th></tr>";
    for(int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      WiFi_Around += "<tr>";
      WiFi_Around += "<td>";
      WiFi_Around += WiFi.SSID(i);
      WiFi_Around += "</td><td>";
      WiFi_Around += WiFi.RSSI(i);
      WiFi_Around += "</td><td>";
      WiFi_Around += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*";
      WiFi_Around += "</td><td>";
      WiFi_Around += BSSIDtoString(WiFi.BSSID(i));
      WiFi_Around += "</td></tr>";
    }
    WiFi_Around += "</table>";
    WiFi.scanDelete();
    return WiFi_Around;
  } // scan
} // namespace avp

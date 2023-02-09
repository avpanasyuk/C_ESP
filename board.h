#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "../C_General/Error.h"

struct ESP8266_board {
  static constexpr char Version[] = "0.0";
  static constexpr uint8_t STR_SIZE = 32;  //< ssid and password string sizes
  AsyncWebServer server;

 protected:
  enum ConnectionStatus_t { IDLE,
                            TRYING_TO_CONNECT,
                            AP_MODE,
                            CONNECTED };
  void (*status_indication_func)(enum ConnectionStatus_t);

  String WiFi_Around;
  IPAddress ip;

 public:
  /**
   * @brief initializes esp8266 board
   *
   * @param Name_ c_str name as seen by DNS
   */
  ESP8266_board(const char *Name,
                void (*status_indication_func_)(enum ConnectionStatus_t),
                const String Usage = "",
                const char *default_ssid = nullptr,
                const char *default_pass = nullptr) : status_indication_func(status_indication_func_),
                                                      WiFi_Around(scan()) {
    // if AutoConnect is enabled the WIFI library tries to connect to the last WiFi configuration that it remembers
    // on startup
    if (WiFi.getAutoConnect()) {
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();
    }

    // trying default WiFI configuration if present
    if (!WiFi.isConnected() && default_ssid != nullptr && default_pass != nullptr) {
      WiFi.mode(WIFI_STA);
      WiFi.hostname(Name);
      WiFi.begin(default_ssid, default_pass);
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();
    }

    // if still not connected switching to AP mode
    if (!WiFi.isConnected()) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(Name, "");
      ip = WiFi.softAPIP();
      status_indication_func(AP_MODE);
      debug_printf("Connecting in AP mode, IP:%s!\n", (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    } else {
      ip = WiFi.localIP();
      debug_printf("Connected in STA mode, IP:%s!\n", (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
      status_indication_func(CONNECTED);
      if (!MDNS.begin(Name)) {  // Start the mDNS responder for esp8266.local
        debug_printf("Error setting up MDNS responder!");
      }
      WiFi.setAutoConnect(true);
      WiFi.setAutoReconnect(true);
    }

    // setup Web Server
    server.on("/", HTTP_GET, [&](AsyncWebServerRequest *request) {
      String content;
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = String("<!DOCTYPE HTML>\r\n<html>Hello from <b>") + Name + "</b> at IP: ";
      content += ipStr + ", MAC: " + WiFi.macAddress() + ", Version: " + Version;
      content += Usage;
      content += "<p>WiFi networks:</p>";
      content += "<p>";
      content += WiFi_Around;
      content += String("</p><form method='get' action='config'><label>SSID: </label><input name='ssid' length=") + (STR_SIZE - 1) +
                 " value='" + WiFi.SSID() + "'><input name='pass' length=" + (STR_SIZE - 1) +
                 "><input type='submit'></html>";
      request->send(200, "text/html", content);
    });

    server.on("/config", HTTP_GET, [&](AsyncWebServerRequest *request) {  // URL xxx.xxx.xxx.xxx/set?pin=14&value=1
      String qsid = request->arg("ssid");
      String qpass = request->arg("pass");
      if (qsid.length() > 0 && qpass.length() > 0) {
        request->send(200, "text/plain", "WiFI configuration changed, connection is being reistablished!");
        delay(1000);
        WiFi.disconnect();
        delay(1000);
        WiFi.mode(WIFI_STA);
        WiFi.begin(qsid, qpass);
        status_indication_func(TRYING_TO_CONNECT);
        WiFi.setAutoConnect(WiFi.waitForConnectResult() == WL_CONNECTED);
        delay(1000);
        ESP.reset();
      }
    });

    AsyncElegantOTA.begin(&server);  // Start ElegantOTA
    server.begin();
  }

 public:
  static const String &scan() {
    String WiFi_Around;
    int n = WiFi.scanNetworks();

    WiFi_Around = "<ol>";
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      WiFi_Around += "<li>";
      WiFi_Around += WiFi.SSID(i);
      WiFi_Around += " (";
      WiFi_Around += WiFi.RSSI(i);

      WiFi_Around += ")";
      WiFi_Around += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
      WiFi_Around += "</li>";
    }
    WiFi_Around += "</ol>";
    return WiFi_Around;
  }  // scan

};  // ESP8266_board
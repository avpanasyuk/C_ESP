/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions, including OTA and a sync server.
 * @details on both board WiFi modules remember the last configuration by it;self, we will rely on it.
 */

#pragma once

#include <ESP8266WebServer.h>
#include "../C_ESP/board_no_server.h"
#include "../C_General/Error.h"

struct ESP_board_sync_server: public ESP_board_no_server {
  ESP8266WebServer server;

protected:
  const char *Version;
  String WiFi_Around;

public:
  /**
   * @brief initializes esp8266 or esp32 board
   *
   * @param Name_ c_str name as seen by DNS
   * @param status_indication_func_ function which will be called by the class when commection status changes
   * @param AddUsage html code for additional commands in "Usage:" descrition, each line starts with "<li>" and ends with "</li>"
   * @param default_ssid if stored configuration failed to connect try this one
   * @param default_pass if stored configuration failed to connect try this one
   */
  ESP_board_sync_server(const char *Name_,
    const char *Version_, 
    void (*status_indication_func_)(enum ConnectionStatus_t),
    const String AddUsage = "",
    const char *default_ssid = nullptr,
    const char *default_pass = nullptr,
    bool ArduinoOTAmDNS = false) : ESP_board_no_server(Name_, status_indication_func_, default_ssid, default_pass, ArduinoOTAmDNS),
    server(80), Version(Version_), WiFi_Around(scan())  {
    
    // setup Web Server
    server.on("/", HTTP_GET, [&, AddUsage]() {
      String content;
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      // debug_printf(Name);
      content = String("<!DOCTYPE HTML>\r\n<html>Hello from <b>") + Name + "</b> at IP: ";
      content += ipStr + ", MAC: " + WiFi.macAddress() + ", Version: " + Version;
      content += F("<p><strong>Usage:</strong><br>"
        "Available URL commands are (like in <em>http://address/command</em>):<ol>"
        "<li> nothing - outputs this screen</li>"
        "<li> pin?i=n - return pin n settings</li>"
        "<li> pin?i=n[&set=(0|1)] - set pin value</li>"
        "<li> pin?i=n[&mode=(0|1)] - set pin mode</li>"
        "<li> config?ssid=<em>string</em>&pass=<em>string</em></li>"
        "<li> reset - reboots MCU</li>"
        "<li> update - update firmware page</li>");
      content += AddUsage;
      content += "</ol></p><p><b>WiFi networks:</b></p>";
      content += "<p>";
      content += WiFi_Around;
      content += String(F("</p><form method='get' action='/config'><label>SSID: </label><input name='ssid' length=")) + (STR_SIZE - 1) +
        " value='" + WiFi.SSID() + "'><input name='pass' length=" + (STR_SIZE - 1) +
        "><input type='submit'></html>";
      server.send(200, "text/html", content);
    });

    server.on("/config", HTTP_GET, [&]() {  // URL xxx.xxx.xxx.xxx/set?pin=14&value=1
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if(qsid.length() > 0 && qpass.length() > 0) {
        server.send(200, "text/plain", "WiFI configuration changed, connection is being reistablished!");
        delay(1000);
        WiFi.disconnect();
        delay(1000);
        WiFi.mode(WIFI_STA);
        WiFi.begin(qsid.c_str(), qpass.c_str());
        status_indication_func(TRYING_TO_CONNECT);
        WiFi.setAutoConnect(WiFi.waitForConnectResult() == WL_CONNECTED);
        delay(1000);
        ESP.restart();
      }
    });

    server.on("/pin", HTTP_GET, [&]() {  // URL xxx.xxx.xxx.xxx/pin?i=n[&analog][&set=x][&mode=x]
      if(server.hasArg("i")) {
        uint8_t Pin = server.arg("i").toInt();
        bool Analog = server.hasArg("analog");
        if(server.hasArg("set") || server.hasArg("mode")) {
          if(server.hasArg("mode")) {
            pinMode(Pin, server.arg("mode").toInt());
            server.send(200, "text/plain", "Pin mode is set!");
          }
          if(server.hasArg("set")) {
            if(Analog)
              analogWrite(Pin, server.arg("set").toInt());
            else
              digitalWrite(Pin, server.arg("set").toInt());
            server.send(200, "text/plain", "Pin is set!");
          }
        } else {
          if(Analog)
            server.send(200, "text/plain", String("Analog pin #") + Pin + " reads " + analogRead(Pin));
          else
            server.send(200, "text/plain", String("Digital pin #") + Pin + " reads " + digitalRead(Pin));
        }
      } else
        server.send(200, "text/plain", "No pin index!");
    });

    server.on("/reset", HTTP_GET, [&]() {
      server.send(200, "text/plain", "Resetting ...");
      delay(1000);
      ESP.restart();
    });

    server.begin();
  }
public:
  static const String scan() {
    String WiFi_Around;
    int n = WiFi.scanNetworks();

    WiFi_Around = "<ol>";
    for(int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      WiFi_Around += "<li>";
      WiFi_Around += WiFi.SSID(i);
      WiFi_Around += " (";
      WiFi_Around += WiFi.RSSI(i);

      WiFi_Around += ")";
      WiFi_Around += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*";
      WiFi_Around += "</li>";
    }
    WiFi_Around += "</ol>";
    return WiFi_Around;
  }  // scan

  void loop() {
    server.handleClient();
    ESP_board_no_server::loop();
  } // loop
};   // ESP_board



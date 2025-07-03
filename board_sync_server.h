/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions, including OTA and a sync server.
 *
 */

#pragma once

#include <memory>
#ifdef ESP8266
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#include <WebServer.h>
#endif
#include "../C_ARDUINO/General.h"
#include "../C_General/Error.hpp"

#include "board_no_server.h"
#include "service.h"

#ifdef ELEGANT_OTA
/**
 * @note - go to .pio/libdeps/espota/ElegantOTA/library.json and remove "dependencies" section. Then remove
 * all "async" librarires in .pio/libdeps
 */
#include <ElegantOTA.h> // include ElegantOTA library into project
#endif

struct ESP_board_sync_server : public ESP_board_no_server {
  WebServer server;

  struct Options_t : public ESP_board_no_server::Options_t {
    const char *Version; //< version of the board
    String AddUsage;     //< additional commands in "Usage:" description
    int LogSize;
  }; // Options_t

  /**
   * return default options
   */
  static Options_t Default() { return {ESP_board_no_server::Default(), "", "", 2000}; } // Default

protected:
  const char *Version;
  String WiFi_Around;
  const std::unique_ptr<avp::Log> pLog;
  static constexpr uint32_t ReservedForResponse = 2048;
  String Response; //< reserve buffer for responses to avoid dynamic memory allocation

public:
  /**
   * @brief initializes esp8266 or esp32 board
   *
   * @param Opts reference to options structure with fields:
   *     - Name c_str name as seen by DNS
   *     - default_ssid if stored configuration failed to connect try this one
   *     - default_pass if stored configuration failed to connect try this one
   *     - status_indication_func function which will be called by the class
   *       when commection status changes
   *     - Version version of the board
   *     - AddUsage additional commands in "Usage:" description
   *     - LogSize size of the log buffer
   */
  ESP_board_sync_server(const Options_t &Opts)
      : ESP_board_no_server(Opts), server(80), Version(Opts.Version), WiFi_Around(scan()),
        pLog(std::make_unique<avp::Log>(Opts.LogSize)) {
    Response.reserve(ReservedForResponse); // reserve buffer for responses to avoid dynamic memory allocation
    Name = Opts.Name;

    // setup Web Server
    on("/", [&, Opts]() {
      String content;
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      // debug_printf(Name);
      Response = String("<!DOCTYPE HTML>\r\n<html>Hello from <b>");
      AddToRespose(Name);
      AddToRespose("</b> at IP: ");
      AddToRespose(ipStr);
      AddToRespose(", MAC: ");
      AddToRespose(WiFi.macAddress());
      AddToRespose(", Version: ");
      AddToRespose(Version);
      AddToRespose("<br>Connected to ");
      AddToRespose(WiFi.SSID());
      AddToRespose(" (BSSID: ");
      AddToRespose(BSSIDtoString(BSSID));
      AddToRespose(F(")<br><p><strong>Usage:</strong><br>"
                     "Available URL commands are (like in <b>http://"));
      AddToRespose(Name);
      AddToRespose(F("/</b><em>command</em>):<ol>"
                     "<li> nothing - outputs this screen</li>"
                     "<li> pin?i=n - return pin n settings</li>"
                     "<li> pin?i=n[&set=(0|1)] - set pin value</li>"
                     "<li> pin?i=n[&mode=(0|1)] - set pin mode</li>"
                     "<li> config?ssid=<em>string</em>&pass=<em>string</em></li>"
                     "<li> log - outputs debug log</li>"
                     "<li> update - update firmware</li>"
                     "<li> reset - reboots MCU</li>"));
      AddToRespose(Opts.AddUsage);
      AddToRespose(F("</ol></p><p><b>WiFi networks:</b></p>"));
      AddToRespose("<p>");
      AddToRespose(WiFi_Around);
      AddToRespose(F("</p><form method='get' action='/config'><label>SSID: </label><input name='ssid' length="));
      AddToRespose(STR_SIZE - 1);
      AddToRespose(" value='");
      AddToRespose(WiFi.SSID());
      AddToRespose("'><input name='pass' length=");
      AddToRespose(STR_SIZE - 1);
      AddToRespose("><input type='submit'></html>");
      send("text/html");
    });

    on("/config", [&]() { // URL xxx.xxx.xxx.xxx/set?pin=14&value=1
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if(qsid.length() > 0 && qpass.length() > 0) {
        send("text/plain", "WiFI configuration changed, connection is being reistablished!");
        delay(1000);
        WiFi.disconnect();
        delay(1000);
        ConnectToBestAP(qsid.c_str(), qpass.c_str());
        if(WiFi.waitForConnectResult() == WL_CONNECTED) StoreAUTH(qsid.c_str(), qpass.c_str());
        delay(1000);
        ESP.restart();
      }
    });

    on("/pin", [&]() { // URL xxx.xxx.xxx.xxx/pin?i=n[&analog][&set=x][&mode=x]
      if(server.hasArg("i")) {
        uint8_t Pin = server.arg("i").toInt();
        bool Analog = server.hasArg("analog");
        if(server.hasArg("set") || server.hasArg("mode")) {
          if(server.hasArg("mode")) {
            pinMode(Pin, server.arg("mode").toInt());
            send("text/plain", "Pin mode is set!");
          }
          if(server.hasArg("set")) {
            if(Analog) analogWrite(Pin, server.arg("set").toInt());
            else digitalWrite(Pin, server.arg("set").toInt());
            send("text/plain", "Pin is set!");
          }
        } else {
          if(Analog) send("text/plain", String("Analog pin #") + Pin + " reads " + analogRead(Pin));
          else send("text/plain", String("Digital pin #") + Pin + " reads " + digitalRead(Pin));
        }
      } else send("text/plain", "No pin index!");
    });

    on("/log", [&]() { send("text/html", avp::GenerateHTML(GetLog(), 2, "LOG")); });

    on("/reset", [&]() {
      send("text/plain", "Resetting ...");
      delay(1000);
      ESP.restart();
    });

#ifdef ELEGANT_OTA
    ElegantOTA.begin(&server); // Initialize ElegantOTA with synchronous server
#endif
    server.begin();
  }

public:
  static const String BSSIDtoString(const uint8_t *BSSID) {
    return avp::String_printf("%02x:%02x:%02x:%02x:%02x:%02x", BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4],
                              BSSID[5]);
  } // BSSIDtoString

  static const String scan() {
    String WiFi_Around; WiFi_Around.reserve(1024); // reserve buffer for response to avoid dynamic memory allocation
    // Scan for WiFi networks
    int n = WiFi.scanNetworks(false, false);

    WiFi_Around = "<table><tr><th>SSID</th><th>RSSI</th><th>Protected</th><th>BSSID</th></tr>";
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

  void AddToLog(const char *s, bool NoBreak = false) { pLog->Add(s, NoBreak); } // AddToLog

  const char *GetLog() const { return pLog->Get(); } // GetLog

  virtual void loop() {
    server.handleClient();
    ESP_board_no_server::loop();
  } // loop

  /**
   * @brief calls server "on" method
   * @param uri
   * @param method
   * @param fn
   */
  void on(const Uri &uri, ESP8266WebServer::THandlerFunction fn, HTTPMethod method = HTTP_GET) {
    server.on(uri, method, fn);
  } // on

  template<typename T>
  void AddToRespose(T x) { Response += String(x); }

  void send(const char *content_type) {
    server.send(200, content_type, Response);
    Response = ""; // clear response buffer
  } // send
  
  void send(const char *content_type, const char *Add) { server.send(200, content_type, Add); }
  void send(const char *content_type, const String &Add) { server.send(200, content_type, Add.c_str()); }
}; // ESP_board_sync_server

template<>
void ESP_board_sync_server::AddToRespose<const char *>(const char *s) {
  Response += s;
}

template<>
void ESP_board_sync_server::AddToRespose<const String &>(const String &s) {
  Response += s;
}

template<>
void ESP_board_sync_server::AddToRespose<const String>(const String s) {
  Response += s;
}

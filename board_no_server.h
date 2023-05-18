/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions, including OTA and async server.
 * @details on both board WiFi modules remember the last configuration by it;self, we will rely on it.
 */

#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#define WIFI_AUTH_OPEN AUTH_OPEN
#else
#include <WiFi.h>
#endif

#include <ArduinoOTA.h>

#include "../C_General/Error.h"

struct ESP_board_no_server {
  enum ConnectionStatus_t {
    IDLE,
    TRYING_TO_CONNECT,
    AP_MODE,
    CONNECTED
  };

static constexpr uint8_t STR_SIZE = 32;  //< ssid and password string sizes

protected:
  void (*status_indication_func)(enum ConnectionStatus_t);

  String WiFi_Around;
  IPAddress ip;
  const char *Name;
  
  void post_connection() {
    ip = WiFi.localIP();
    debug_printf("Connected in STA mode, IP:%s!\n", (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    status_indication_func(CONNECTED);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  }  // post_connection

public:
  /**
   * @brief initializes esp8266 or esp32 board
   *
   * @param Name_ c_str name as seen by DNS
   * @param status_indication_func_ function which will be called by the class when commection status changes
   * @param default_ssid if stored configuration failed to connect try this one
   * @param default_pass if stored configuration failed to connect try this one
   */
  ESP_board_no_server(const char *Name_,
    void (*status_indication_func_)(enum ConnectionStatus_t),
    const char *default_ssid = nullptr,
    const char *default_pass = nullptr): status_indication_func(status_indication_func_), WiFi_Around(scan()),
    Name(Name_) {
    // if AutoConnect is enabled the WIFI library tries to connect to the last WiFi configuration that it remembers
    // on startup
    if(WiFi.getAutoConnect()) {
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();
    }

    // trying default WiFI configuration if present
    if(!WiFi.isConnected() && default_ssid != nullptr && default_pass != nullptr) {
      WiFi.mode(WIFI_STA);
      WiFi.hostname(Name);
      WiFi.begin(default_ssid, default_pass);
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();
    } else
      post_connection();

    // if still not connected switching to AP mode
    if(!WiFi.isConnected()) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(Name, "");
      ip = WiFi.softAPIP();
      status_indication_func(AP_MODE);
      debug_printf("Connecting in AP mode, IP:%s!\n", (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    } else
      post_connection();

    ArduinoOTA.begin(false);
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
};   // ESP_board

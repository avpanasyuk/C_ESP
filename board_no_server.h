/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions, including OTA.
 * @details on both board WiFi modules remember the last configuration by it;self, we will rely on it.
 * @note NOTE!!!!!!!!!!!!!! Should add ArduinoOTA.handle() to main loop !!!!!!!!!!!!!!!!!!!!!!!!
 */

#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h>  // https://github.com/esp8266/Arduino
#define WIFI_AUTH_OPEN AUTH_OPEN
#else
#include <WiFi.h>
#endif

#ifndef DO_OTA // !!!!!!!!!!!! DO NOT FORGET TO CALL ArduinoOTA.handle() from the loop()
#define DO_OTA 1 // default on 
#endif

#if DO_OTA
#include <ArduinoOTA.h>
#endif


#include "../C_General/Error.h"

struct ESP_board_no_server {
  enum ConnectionStatus_t {
    IDLE,
    TRYING_TO_CONNECT,
    AP_MODE,
    CONNECTED
  };

  static constexpr uint8_t STR_SIZE = 32;  //< ssid and password string sizes
  typedef std::function<void(enum ConnectionStatus_t)> status_indication_func_t;
protected:

  status_indication_func_t status_indication_func;

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
    const char *default_ssid,
    const char *default_pass,
    status_indication_func_t status_indication_func_ = [](enum ConnectionStatus_t) { }) :
    Name(Name_), status_indication_func(status_indication_func_) {
    // if AutoConnect is enabled the WIFI library tries to connect to the last WiFi configuration that it remembers
    // on startup
    if(WiFi.getAutoConnect()) {
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();
    }

    if(WiFi.isConnected()) post_connection();
    else if(default_ssid != nullptr && default_pass != nullptr) { // trying default WiFI configuration if present
      WiFi.mode(WIFI_STA);
      if(Name != nullptr && Name[0]) WiFi.setHostname(Name); 
      WiFi.begin(default_ssid, default_pass);
      status_indication_func(TRYING_TO_CONNECT);
      WiFi.waitForConnectResult();

      if(WiFi.isConnected()) post_connection();
      else {  // if still not connected switching to AP mode. In this mode we got to connect to ESP generated WiFi
        WiFi.mode(WIFI_AP);
        WiFi.softAP(Name, "");
        ip = WiFi.softAPIP();
        status_indication_func(AP_MODE);
        debug_printf("Connecting in AP mode, IP:%s!\n", (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
      }
    }

    // debug_puts(MDNS.begin(Name) ? "mDNS responder started" : "Error starting mDNS");
    
#if DO_OTA
    ArduinoOTA.onStart([]() {
      debug_puts(ArduinoOTA.getCommand() == U_FLASH?"sketch":"fs");
    });
    ArduinoOTA.onEnd([]() { debug_puts("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      debug_printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      debug_printf("Error[%u]: ", error);
      if(error == OTA_AUTH_ERROR) debug_puts("Auth Failed");
      else if(error == OTA_BEGIN_ERROR) debug_puts("Begin Failed");
      else if(error == OTA_CONNECT_ERROR) debug_puts("Connect Failed");
      else if(error == OTA_RECEIVE_ERROR) debug_puts("Receive Failed");
      else if(error == OTA_END_ERROR) debug_puts("End Failed");
    });
    ArduinoOTA.begin();
#endif
  }

  String getIP() const { return ip.toString(); }

  virtual void loop() {
#if DO_OTA
    ArduinoOTA.handle();
    yield();
#endif
  } // loop
};   // ESP_board


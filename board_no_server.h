/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions,
 * including OTA.
 * @details * WiFi configuration is stored in EEPROM, which is simulated in flash really
 * Module tries to connect to stored WiFi first, and go to AP mode if not successful
 * You can connect to AP WiFi network, go to 192.168.4.1 and set WiFi connection
 * (or control switch).
 */

#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h> // https://github.com/esp8266/Arduino
#include <ESP8266mDNS.h>
#define WIFI_AUTH_OPEN AUTH_OPEN
#else
#include <ESPmDNS.h>
#include <WiFi.h>

#endif

#ifndef DO_OTA   // !!!!!!!!!!!! DO NOT FORGET TO CALL ArduinoOTA.handle() from
                 // the loop()
#define DO_OTA 1 // default on. SET DO_OTA to 0 to really turn it off
#endif

#if DO_OTA
#include <ArduinoOTA.h>
#endif

#include "../C_General/Error.h"

struct ESP_board_no_server {
  static enum ConnectionStatus_t { IDLE, TRYING_TO_CONNECT, AP_MODE, CONNECTED } ConnStatus; // static so it can be reached from an interrupt

  typedef std::function<void(enum ConnectionStatus_t)> status_indication_func_t;

  struct Options_t {
    const char *Name;                                 //< name of the device as seen by DNS
    const char *default_ssid;                         //< default ssid to connect to if stored
                                                      // configuration failed
    const char *default_pass;                         //< default password to connect to if stored
                                                      // configuration failed
    status_indication_func_t status_indication_func_; //< function which will be called by the class
                                                      // when connection status changes
  };

  /**
   * return default options
   */
  static Options_t Default() {
    return {"", "L", "group224", [](enum ConnectionStatus_t s) { ConnStatus = s; }};
  } // Default

  static constexpr uint8_t STR_SIZE = 32; //< ssid and password string sizes
protected:
  IPAddress ip;
  const char *Name;
  status_indication_func_t status_indication_func;

public:
  /**
   * @brief initializes esp8266 or esp32 board
   *
   * @param Name_ c_str name as seen by DNS
   * @param status_indication_func_ function which will be called by the class
   * when commection status changes
   * @param default_ssid if stored configuration failed to connect try this one
   * @param default_pass if stored configuration failed to connect try this one
   */
  ESP_board_no_server(
      const char *Name_, const char *default_ssid, const char *default_pass,
      status_indication_func_t status_indication_func_ = [](enum ConnectionStatus_t) {})
      : Name(Name_), status_indication_func(status_indication_func_) {
    // if AutoConnect is enabled the WIFI library tries to connect to the last
    // WiFi configuration that it remembers on startup
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
      else open_AP();
    }

    MDNS.begin(Name);

#if DO_OTA
    ArduinoOTA.setHostname(Name);

    ArduinoOTA.onStart([]() { debug_puts(ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "fs"); });
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
  } // constructor
  ESP_board_no_server(const Options_t &Opts)
      : ESP_board_no_server(Opts.Name, Opts.default_ssid, Opts.default_pass, Opts.status_indication_func_) {}

  void open_AP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(Name, "");
    ip = WiFi.softAPIP();
    status_indication_func(AP_MODE);
    debug_printf("Connecting in AP mode, IP:%s!\n",
                 (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
  } // open_AP

  void post_connection() {
    ip = WiFi.localIP();
    debug_printf("Connected in STA mode, IP:%s!\n",
                 (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    status_indication_func(CONNECTED);
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
  } // post_connection

  void reconnect() {
    status_indication_func(TRYING_TO_CONNECT);
    WiFi.mode(WIFI_STA);
    WiFi.reconnect();
    WiFi.waitForConnectResult();
    if(WiFi.isConnected()) post_connection();
    else open_AP();
  } // reconnect

  String getIP() const { return ip.toString(); }

  virtual void loop() {
#if DO_OTA
    ArduinoOTA.handle();
#endif
    MDNS.update();
    // yield();
  } // loop
}; // ESP_board

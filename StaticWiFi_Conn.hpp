/**
 * @author Sasha
 *
 * @brief class for ESP8266 or ESP32, implements commonly used WiFi functions,
 * including OTA.
 * @details * WiFi configuration is stored in EEPROM, which is simulated in
 * flash really Module tries to connect to stored WiFi first, and go to AP mode
 * if not successful You can connect to AP WiFi network, go to 192.168.4.1 and
 * set WiFi connection (or control switch).
 */

#pragma once
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h> // https://github.com/esp8266/Arduino
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#define WIFI_AUTH_OPEN AUTH_OPEN
#endif

#if ESP32
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#endif

#include <LittleFS.h> // I do not want to use autoConnect, lets store SSID and password
                      // in LittleFS. you should put board_build.filesystem = littlefs in
                      // platformio.ini

static const char *LittleFS_AUTH = "/net_auth.txt";

#if defined(DO_OTA) && DO_OTA
#include <ArduinoOTA.h>
#endif

#ifndef NAME
#define NAME "DefineNAME"
#endif

#include <Arduino.h>
#include "C_ARDUINO/General.h"
#include "C_General/Error.hpp"
#include "C_General/millis_micros.hpp"
#include "C_General/MyTime.hpp"
#include "fast_gpio.hpp"
#include "hw_timer.hpp" // I have to run Blinken from hardware interrupt, as loop() runs after everything
// is set already
#include "service.h"

WiFiMulti wifiMulti;

namespace avp {
  struct StaticWiFi_Conn {
    static inline enum class Status_t {
      BEFORE_BEGIN,
      IDLE,
      TRYING_TO_CONNECT,
      AP_MODE,
      CONNECTED
    } ConnStatus = Status_t::BEFORE_BEGIN; // static so it can be reached from an interrupt

    typedef void (*status_indication_func_t)(); ///< should be declared IRAM_ATTR
                                                // static void IRAM_ATTR idle() {};

#ifdef LED_BUILTIN
    template<uint8_t LED_Pin = LED_BUILTIN>
#else
    template<uint8_t LED_Pin>
#endif
    static void AVP_RAM_ATTR Blinken() { // should be run once in 200 ms or so
      static int Counter = 0;

      switch(ConnStatus) {
      case Status_t::IDLE:
        avp::SetPin<LED_Pin>(); // LED off
        break;
      case Status_t::TRYING_TO_CONNECT:
        if(++Counter > 1) {
          Counter = 0;
          avp::TogglePin<LED_Pin>();
        }
        break;
      case Status_t::AP_MODE:
        if(++Counter > 4) {
          Counter = 0;
          avp::TogglePin<LED_Pin>();
        }
        break;
      case Status_t::CONNECTED:
        avp::ClearPin<LED_Pin>(); // LED on
        break;
      } // switch (Stat)
    } // Blinken

    struct Options_t {
      const char *Name;                                 ///< name of the device as seen by DNS
      const char *default_ssid;                         ///< default ssid to connect to if stored
                                                        // configuration failed
      const char *default_pass;                         ///< default password to connect to if stored
                                                        // configuration failed
      status_indication_func_t status_indication_func_; ///< function which will be
                                                        // called every 100 ms with
                                                        // connection status
    };

    static const Options_t &DefaultOpts() {
      static Options_t Opts{NAME, "L", "group224", Blinken};
      return Opts;
    } // Default

    static inline bool OTA_IsInProgress;

    static constexpr uint8_t STR_SIZE = 32; ///< ssid and password string sizes
  protected:
    static inline IPAddress ip;
    static inline const char *Name;
    static inline String ssid, pass;
    static inline status_indication_func_t status_indication_func;
    static inline String ConnectedNode;
    // static inline uint8_t BSSID[6]; ///< BSSID of the best AP

  public:
    /**
     * @brief there is static class, no contructor. begin() initializes the WiFI connection
     *
     * @param Name_ c_str name as seen by DNS
     * @param status_indication_func_ function which will be called by the class
     * when commection status changes
     * @param default_ssid if stored configuration failed to connect try this one
     * @param default_pass if stored configuration failed to connect try this one
     */
    static void begin(const char *Name_, const char *default_ssid, const char *default_pass,
      status_indication_func_t status_indication_func_) {
      if(ConnStatus != Status_t::BEFORE_BEGIN) return; // make sure we run begin once only

      ConnStatus = Status_t::IDLE;
      OTA_IsInProgress = false;
      Name = Name_;
      ssid = default_ssid;
      pass = default_pass;
      status_indication_func = status_indication_func_;

      // I have to setup Blinken here to see all connection process
      avp::HW_Timer_ms<>::CreateTimer([]() {
        status_indication_func();
        return true;
      },
        200, true);
      WiFi.setAutoReconnect(false);
      WiFi.setAutoConnect(false); // do not try to connect to the last known AP, because I use multiWiFi to do that
#ifdef ESP32
      LittleFS.begin(true);
#endif
#ifdef ESP8266
      LittleFS.begin();
#endif
      if(LittleFS.exists(LittleFS_AUTH)) {
        File f = LittleFS.open(LittleFS_AUTH, "r");
        if(f) {
          ssid = f.readStringUntil('\n');
          pass = f.readStringUntil('\n');
          debug_printf("Stored credentials: %s, %s\n", ssid.c_str(), pass.c_str());
        } else debug_puts("Failed to open stored credentials file!\n");
      } else debug_puts("No stored credentials found!\n");

      if(ssid == "") open_AP();
      else ConnectToBestAP(ssid.c_str(), pass.c_str());

      MDNS.begin(Name);

#if defined(DO_OTA) && DO_OTA
      ArduinoOTA.setHostname(Name);

      ArduinoOTA.onStart([]() {
        debug_puts(ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "fs");
#ifdef ESP32
        WiFi.setSleep(false); // prevents WiFi from napping
#endif
#ifdef ESP8266
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
        delay(100);
        DEBUG_PUT_PLACE
        OTA_IsInProgress = true;
      });
      ArduinoOTA.onEnd([]() {
        WiFi.setSleep(true);
        debug_puts("\nEnd");
        OTA_IsInProgress = false;
      });
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
        OTA_IsInProgress = false;
      });
      ArduinoOTA.begin();
#endif

    } // constructor

    static void begin(const Options_t &Opts = DefaultOpts()) {
      begin(Opts.Name, Opts.default_ssid, Opts.default_pass, Opts.status_indication_func_);
    } // begin

    static void ConnectToBestAP(const char *SSID, const char *Pass) {
      WiFi.mode(WIFI_STA);
      if(Name != nullptr && Name[0]) {
#if defined(ESP32)
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE); // hack to make sure setHostname works
#endif
        WiFi.setHostname(Name);
      }
      wifiMulti.addAP(SSID, Pass);
      if(wifiMulti.run() == WL_CONNECTED) post_connection();
      else open_AP();
    } // ConnectToBestAP

    static void open_AP() {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(Name, "");
      ip = WiFi.softAPIP();
      ConnStatus = Status_t::AP_MODE;
      debug_printf("Waiting for connection in AP mode, IP:%s!\n",
        (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    } // open_AP

    static void post_connection() {
      ip = WiFi.localIP();
      debug_printf("Connected in STA mode, IP:%s!\n", getIP().c_str());
      ConnectedNode = String("SSID: ") + WiFi.SSID() +
                      ", BSSID: " + WiFi.BSSIDstr() + ", RSSI: " + WiFi.RSSI();
      ConnStatus = Status_t::CONNECTED;
    } // post_connection

    static Status_t GetStatus() { return ConnStatus; }
    static bool IsConnected() { return GetStatus() == Status_t::CONNECTED; }

    static String getIP() { return ip.toString(); }

    static const String &Greeting() {
      static String Resp;
      Resp.reserve(200);
      Resp = "Hello from <b>";
      Resp += Name;
      Resp += "</b> at IP: ";
      Resp += getIP();
      Resp += ", MAC: ";
      Resp += WiFi.macAddress();
      Resp += "<br>Flash (bytes): ";
      Resp += ESP.getFlashChipSize();
      Resp += ", Free Heap (bytes): ";
      Resp += ESP.getFreeHeap();
      Resp += "<br>Connected to ";
      Resp += ConnectedNode;
      return Resp;
    } // Greeting

    static void StoreAUTH(const char *SSID, const char *Pass) {
      File f = LittleFS.open(LittleFS_AUTH, "w");
      if(f) {
        f.print(SSID);
        f.print('\n');
        f.print(Pass);
        f.print('\n');
        debug_puts("Stored credentials\n");
      } else debug_puts("Failed to open stored credentials file!\n");
    } // StoreAUTH

    static void call_in_loop() {
      //      static avp::TimePeriod1<10UL * 60 * 1000, millis> TP;
      //      if(TP.Expired()) TryToConnect(); // try to connect every 10 minutes
      if(!WiFi.isConnected()) {
        if(wifiMulti.run(10000) == WL_CONNECTED) post_connection();
        else open_AP();
      }

#if defined(DO_OTA) && DO_OTA
      ArduinoOTA.handle();
#endif
#if defined(ESP8266)
      MDNS.update();
#endif
      // delay(1);
      yield();
    } // call_in_loop
  }; // struct StaticWiFi_Conn
} // namespace avp
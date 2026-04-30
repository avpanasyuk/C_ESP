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
#include <ESP8266mDNS.h>
#define WIFI_AUTH_OPEN AUTH_OPEN
#else
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#include <LittleFS.h> // I do not want to use autoConnect, lets store SSID and password
                      // in LittleFS. you should put board_build.filesystem = littlefs in
                      // platformio.ini

inline constexpr const char *LittleFS_AUTH = "/net_auth.txt";

#if defined(DO_OTA) && DO_OTA
#include <ArduinoOTA.h>
#endif

#if defined(ESP8266)
#define WIFI_MODE_STA WIFI_STA
#define WIFI_MODE_AP WIFI_AP
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

namespace avp {
  struct StaticWiFi_Conn {
    static inline volatile enum class Status_t {
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

    static constexpr uint8_t STR_SIZE = 32;         ///< ssid and password string sizes
    static constexpr int32_t MinRSSIdiffToJump = 8; // dB
  protected:
    static inline IPAddress ip;
    static inline const char *Name;
    static inline String ssid, pass;
    static inline status_indication_func_t status_indication_func;
    static inline avp::TimePeriod1<10UL * 1000, millis> ConnectionTO;           // 10 seconds
    static inline avp::TimePeriod1<10UL * 60 * 1000, millis> AP_MODE_ACTIVE_TO; // 10 seconds

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
      WiFi.setAutoConnect(false); // do not try to connect to the last known AP, because I want to
                                  // connect to the one with the best RSSI
                                  // but if I have some stored credentials use them instead of default ones
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
          ssid.trim();
          pass = f.readStringUntil('\n');
          pass.trim();
          debug_printf("Stored credentials: %s, %s\n", ssid.c_str(), pass.c_str());
        } else debug_puts("Failed to open stored credentials file!\n");
      } else debug_puts("No stored credentials found!\n");

      if(ssid == "") open_AP();
      else {
#ifdef NAME
        WiFi.setHostname(NAME);
#endif
        WiFi.mode(WIFI_STA);
        scanNetworks(false, ssid.c_str());
        BeginConnectingToBestAP(ssid.c_str(), pass.c_str());
        WiFi.waitForConnectResult();
        CheckConnection();
      }
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

    /**
     * @brief Should be called after scanNetworks has run or repeatedly if it was started asynchroneously
     *
     * @param SSID
     * @param Pass
     */
    static void BeginConnectingToBestAP(const char *SSID, const char *Pass) {
      uint8_t BestRSSI_i;
      const char *ErrorMsg(FindTheBestAPinScan(BestRSSI_i));
      if(ErrorMsg != nullptr) debug_puts(ErrorMsg);
      else {                                                            // found some APs
        if(WiFi.status() == WL_CONNECTED) {                             // we will have to compare this scan RSSI with what we have now
          if(WiFi.RSSI(BestRSSI_i) > WiFi.RSSI() + MinRSSIdiffToJump) { // best RSSI is way better
            WiFi.disconnect();
            WiFi.mode(WIFI_STA);
            WiFi.begin(SSID, Pass, WiFi.channel(BestRSSI_i), WiFi.BSSID(BestRSSI_i));
            ConnStatus = Status_t::TRYING_TO_CONNECT;
            ConnectionTO.Reset();
          }
        } else { // not connected
          WiFi.mode(WIFI_STA);
          WiFi.begin(SSID, Pass, WiFi.channel(BestRSSI_i), WiFi.BSSID(BestRSSI_i));
          ConnStatus = Status_t::TRYING_TO_CONNECT;
          ConnectionTO.Reset();
        }
        // WiFi.waitForConnectResult(); // let's not do blocking
      }
      WiFi.scanDelete();
    } // BeginConnectingToBestAP

    static void CheckConnection() {
      switch(WiFi.status()) {
      case WL_CONNECTED:
        ip = WiFi.localIP();
        debug_printf("Connected in STA mode, IP:%s!\n", getIP().c_str());
        ConnStatus = Status_t::CONNECTED;
        break;
      case WL_IDLE_STATUS:
        break;
      case WL_DISCONNECTED:
        if(!ConnectionTO.JustCheck()) break; // Timout is not over yet
      default:
        open_AP();
        break;
      }
    } // CheckConnection()

    static void open_AP() {
      WiFi.mode(WIFI_AP); // WIFI_AP does not allow scans
      WiFi.softAP(Name, "");
      ip = WiFi.softAPIP();
      AP_MODE_ACTIVE_TO.Reset();
      ConnStatus = Status_t::AP_MODE;
      debug_printf("Waiting for connection in AP mode, IP:%s!\n",
        (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str());
    } // open_AP

    static String getIP() { return ip.toString(); }

    static const String &Greeting() {
      static String Resp;
      Resp.reserve(512);
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
      if(WiFi.getMode() == WIFI_MODE_STA) {
        Resp += "<br>Connected to ";
        Resp += WiFi.SSID();
        Resp += " (BSSID: ";
        Resp += BSSIDtoString(WiFi.BSSID());
        Resp += ")";
      } else Resp += ", (AP mode)";
      return Resp;
    } // Greeting

    static void StoreAUTH(const char *SSID, const char *Pass) {
      ssid = SSID;
      pass = Pass;

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
      static avp::TimePeriod1<10UL * 60 * 1000, millis> TP;
      if(WiFi.getMode() == WIFI_MODE_STA) {
        if(WiFi.scanComplete() != WIFI_SCAN_RUNNING &&
           !OTA_IsInProgress && TP.Expired()) {
          // try to find better AP every 10 minutes
          scanNetworks(true, ssid.c_str());
        }

        if(WiFi.scanComplete() > 0)
          BeginConnectingToBestAP(ssid.c_str(), pass.c_str());
      }

      if(ConnStatus == Status_t::AP_MODE) { // WiFi.getMode() in WIFI_AP mode has no clue
        if(WiFi.softAPgetStationNum() > 0) AP_MODE_ACTIVE_TO.Reset();
        if(AP_MODE_ACTIVE_TO.Expired()) { // nothing is using AP, trying to connect again
          scanNetworks(false, ssid.c_str());
          if(WiFi.scanComplete() > 0)
            BeginConnectingToBestAP(ssid.c_str(), pass.c_str());
        }
      }

      if(ConnStatus == Status_t::TRYING_TO_CONNECT) CheckConnection();

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
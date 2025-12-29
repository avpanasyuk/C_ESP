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
#include "hw_timer.hpp"
#include "service.h"

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
    static void IRAM_ATTR idle() {};

#ifdef LED_BUILTIN
    template<uint8_t LED_Pin = LED_BUILTIN>
#else
    template<uint8_t LED_Pin>
#endif
    static void IRAM_ATTR Blinken() { // should be run once in 200 ms or so
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

    static const Options_t &DefaultOpts() { static Options_t Opts{NAME, "L", "group224", idle}; return Opts; } // Default
    
    static inline bool OTA_IsInProgress;

    static constexpr uint8_t STR_SIZE = 32; ///< ssid and password string sizes
  protected:
    static inline IPAddress ip;
    static inline const char *Name;
    static inline String ssid, pass;
    static inline status_indication_func_t status_indication_func;
    static inline uint8_t BSSID[6]; ///< BSSID of the best AP

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
      status_indication_func_t status_indication_func_ = idle) {
      AVP_ASSERT(ConnStatus == Status_t::BEFORE_BEGIN); // make sure we run begin once only

      ConnStatus = Status_t::IDLE;
      OTA_IsInProgress = false;
      Name = Name_;
      ssid = default_ssid;
      pass = default_pass;
      status_indication_func = status_indication_func_;

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
        DEBUG_LINE_NUM
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
      if(FindBestAP(SSID, BSSID)) {
        debug_printf("Trying to connect to %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n", SSID, BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
        WiFi.mode(WIFI_STA);
        if(Name != nullptr && Name[0]) WiFi.setHostname(Name);
        WiFi.begin(SSID, Pass, 0, BSSID);
        ConnStatus = Status_t::TRYING_TO_CONNECT;
        WiFi.waitForConnectResult();
      }
      if(WiFi.isConnected()) post_connection();
      else {
        LittleFS.remove(LittleFS_AUTH); // remove stored credentials
        debug_puts("Stored credentials removed!\n");
        open_AP();
      } // defaults are not present or do not work, go to AP mode
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
      ConnStatus = Status_t::CONNECTED;
      WiFi.setAutoConnect(false);
      WiFi.setAutoReconnect(false);
    } // post_connection

    static void TryToConnect() {
      if(!WiFi.isConnected()) {
        ConnectToBestAP(ssid.c_str(), pass.c_str());
        if(WiFi.isConnected()) post_connection();
        else open_AP();
      }
    } // TryToConnect

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
      Resp += WiFi.SSID();
      Resp += " (BSSID: ";
      Resp += BSSIDtoString(BSSID);
      Resp += ")";
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
      static avp::TimePeriod1<10UL * 60 * 1000, millis> TP;
      if(TP.Expired()) TryToConnect(); // try to connect every 10 minutes

#if defined(DO_OTA) && DO_OTA
      ArduinoOTA.handle();
#endif
#if defined(ESP8266)
      MDNS.update();
#endif
      // delay(1);
      yield();
    } // call_in_loop

    static String BSSIDtoString(const uint8_t *BSSID) {
      return avp::String_printf("%02x:%02x:%02x:%02x:%02x:%02x", BSSID[0], BSSID[1], BSSID[2], BSSID[3], BSSID[4], BSSID[5]);
    } // BSSIDtoString

    /**
     * @brief scans all available networks and returns a table
     *
     * @return const String& - HTML formatted table
     */
    static const String &scan() {
      static String WiFi_Around;
      WiFi_Around.reserve(300); // reserve buffer for response to avoid dynamic memory allocation
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
  }; // struct StaticWiFi_Conn
} // namespace avp
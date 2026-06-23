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
      SCANNING,
      CONNECTED
    } ConnStatus = Status_t::BEFORE_BEGIN; // static so it can be reached from an interrupt

    typedef void (*status_indication_func_t)(); ///< should be declared IRAM_ATTR
                                                // static void IRAM_ATTR idle() {};

    // LED pin used by Blinken(). Set before passing &Blinken as status_indication_func_.
    // Runtime parameter (not template) so the IRAM_ATTR actually sticks — GCC doesn't
    // reliably place template instantiations in the .iram0.text section.
    static inline uint8_t LED_pin =
#ifdef LED_BUILTIN
      LED_BUILTIN;
#else
      0xFF;
#endif

    /**
     * @brief I have tried to make it a template on LEB_BUILTIN, but templates do not implement IRAM_ATTR 
     * attribute which caused crashes when Blinken was called from ISR
     * 
     */
    static void IRAM_ATTR Blinken() { // should be run once in 200 ms or so
      static int Counter = 0;

      switch(ConnStatus) {
      case Status_t::BEFORE_BEGIN:
      case Status_t::IDLE:
        avp::SetPin(LED_pin); // LED off
        break;
      case Status_t::TRYING_TO_CONNECT:
        if(++Counter > 1) {
          Counter = 0;
          avp::TogglePin(LED_pin);
        }
        break;
      case Status_t::AP_MODE:
        if(++Counter > 4) {
          Counter = 0;
          avp::TogglePin(LED_pin);
        }
        break;
      case Status_t::SCANNING:
        if(++Counter > 8) {
          Counter = 0;
          avp::TogglePin(LED_pin);
        }
        break;
      case Status_t::CONNECTED:
        avp::ClearPin(LED_pin); // LED on
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
    static inline uint32_t OTA_LastActivity_ms;            ///< millis() of last OTA start/progress, for the stall watchdog
    static constexpr uint32_t OTA_StallTimeout_ms = 30000; ///< no OTA progress for this long => assume the upload died

    static constexpr uint8_t STR_SIZE = 32;          ///< ssid and password string sizes
    static constexpr int32_t MinRSSIdiffToJump = 20; // dB
  protected:
    static inline IPAddress ip;
    static inline const char *Name;
    static inline String ssid, pass;
    static inline status_indication_func_t status_indication_func;
    static inline avp::TimePeriod1<10UL * 1000, millis> ConnectionTO;           // 10 seconds
    static inline avp::TimePeriod1<10UL * 60 * 1000, millis> AP_MODE_ACTIVE_TO; // 10 minutes
    static inline avp::TimePeriod1<10UL * 60 * 1000, millis> RescanTO;          // time to rescan AP, maybe we can get new connection
    static inline int32_t Channel = 0;

    static bool AVP_RAM_ATTR status_tick() { status_indication_func(); return true; }
    static inline avp::HW_Timer_ms<>::Timer_t *pTimer = nullptr;

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
      pTimer = &avp::HW_Timer_ms<>::CreateTimer(status_tick, 200, true); // can not use lambda here as 
      // it does not have IRAM_ATTR

      WiFi.persistent(false); // should be the first WiFi call
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
          debug_printf("Stored credentials for SSID: %s\n", ssid.c_str()); // never log the password
        } else debug_puts("Failed to open stored credentials file!\n");
      } else debug_puts("No stored credentials found!\n");

      if(ssid == "") open_AP();
      else {
        WiFi.setHostname(Name); // configured per-device name, consistent with MDNS/OTA/softAP below

        // Let's see what we have here
        WiFi.mode(WIFI_STA);
        scanNetworks(false);
        if(CheckScanAndTryToConnectToBestAP(ssid.c_str(), pass.c_str())) {
          WiFi.waitForConnectResult();
          if(WiFi.status() == WL_CONNECTED) ConfirmConnected();
          else open_AP(); // if anything went wrong open AP
        }
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
        // NB: do NOT read flash string literals (DEBUG_PUT_PLACE, __PRETTY_FUNCTION__, F("..."))
        // inside OTA callbacks — ArduinoOTA has already precached IROM and other pages may fault.
        OTA_LastActivity_ms = millis();
        OTA_IsInProgress = true;
      });
      ArduinoOTA.onEnd([]() {
        WiFi.setSleep(true);
        debug_puts("\nEnd");
        OTA_IsInProgress = false;
      });
      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        OTA_LastActivity_ms = millis();
        debug_printf("Progress: %u%%\r", (progress / (total / 100)));
      });

      ArduinoOTA.onError([](ota_error_t error) {
        WiFi.setSleep(true);
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

    static void scanNetworks(bool IsAsync, const char *ssid_str = ssid.c_str()) {
#ifdef ESP8266
      WiFi.scanNetworks(IsAsync, false, Channel, (uint8_t *)ssid_str);
#endif

#ifdef ESP32
      WiFi.scanNetworks(IsAsync, false, false, 300U, Channel, ssid_str);
#endif
    } // scanNetworks

    static void ConfirmConnected() {
      IPAddress newIP = WiFi.localIP();
      if((uint32_t)newIP == 0) {
        // WL_CONNECTED reports only L2 association; DHCP may still be in flight.
        // Stay in current state so the caller re-enters next loop iteration.
        // Critical: LEAmDNS::_joinMulticastGroups skips interfaces with no IP,
        // so calling MDNS.notifyAPChange() before DHCP completes leaves mDNS
        // silently broken until *another* AP change -- which never comes.
        return;
      }
      // Log every IP change (release + debug); same-IP roams stay silent so
      // /log shows only meaningful network events.
      bool ip_changed = (ip != newIP);
      ip = newIP;
      if(ip_changed)
        debug_printf("Connected in STA mode, IP:%s!\n", getIP().c_str());
      ConnStatus = Status_t::CONNECTED;
      RescanTO.Reset();
#if defined(ESP8266)
      // Re-publish mDNS records on every (re)connect that brought a fresh IP.
      // Fires from ALL ConfirmConnected paths -- not just TRYING_TO_CONNECT --
      // so e.g. a router reset that the chip recovers from without losing
      // WL_CONNECTED still refreshes the mDNS UDP socket.
      MDNS.notifyAPChange();
#endif
    } // ConfirmConnected

    /**
     * @brief Should be called after scanNetworks has successfully completed sync or Async.
     * Deletes scan at the end
     *
     * @param SSID
     * @param Pass
     * @retval I have called WiFi.begin() and actually trying to connect
     */
    static bool CheckScanAndTryToConnectToBestAP(const char *SSID, const char *Pass) {
      if(WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        avp::CallWhenOutOfScope _([]() {
          WiFi.scanDelete();
        });

        uint8_t BestRSSI_i;
        if(FindTheBestAPinScan(BestRSSI_i, Channel)) {
          if(WiFi.status() == WL_CONNECTED && WiFi.RSSI(BestRSSI_i) < WiFi.RSSI() + MinRSSIdiffToJump) {
            // or current RSSI is fine
            ConfirmConnected();
          } else {
            WiFi.mode(WIFI_STA);
            WiFi.begin(SSID, Pass, WiFi.channel(BestRSSI_i), WiFi.BSSID(BestRSSI_i));
            ConnStatus = Status_t::TRYING_TO_CONNECT;
            ConnectionTO.Reset();
            return true;
          }
        } else {
          if(WiFi.status() != WL_CONNECTED) open_AP();
          else ConfirmConnected();
        }
      }
      return false;
    } // CheckScanAndTryToConnectToBestAP

    static void open_AP() {
      // Capture the STA status we're abandoning *before* switching the radio to AP
      // mode, so /log records WHY every fallback happened (not just that it did).
      // ESP8266 wl_status_t: 0=IDLE 1=NO_SSID_AVAIL 2=SCAN_DONE 3=CONNECTED
      //                      4=CONNECT_FAILED 5=CONNECTION_LOST 6=WRONG_PASSWORD 7=DISCONNECTED.
      int sta_status = WiFi.status();
      WiFi.mode(WIFI_AP);
      WiFi.softAP(Name, "");
      ip = WiFi.softAPIP();
      AP_MODE_ACTIVE_TO.Reset();
      ConnStatus = Status_t::AP_MODE;
      debug_printf("Waiting for connection in AP mode, IP:%s! (left STA status=%d)\n",
        (String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3])).c_str(),
        sta_status);
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

    static void AsyncStartScanNetworks() {
      ConnStatus = Status_t::SCANNING;
      WiFi.mode(WIFI_STA);
      scanNetworks(true);
    } // AsyncScanNetworks

    static void call_in_loop() {
      // this function si usual a state machine
      // OK, let's figure out what we are doing at the moment
      switch(ConnStatus) {
      case Status_t::CONNECTED: // I think I am connected
        if(WiFi.status() != WL_CONNECTED) AsyncStartScanNetworks();
        else {
          if(!OTA_IsInProgress && RescanTO.Expired()) AsyncStartScanNetworks();
        }
        break;
      case Status_t::AP_MODE:
        if(WiFi.softAPgetStationNum() > 0) AP_MODE_ACTIVE_TO.Reset();
        if(AP_MODE_ACTIVE_TO.Expired()) AsyncStartScanNetworks();
        break;
      case Status_t::SCANNING:
        CheckScanAndTryToConnectToBestAP(ssid.c_str(), pass.c_str());
        break;
      case Status_t::TRYING_TO_CONNECT:
        switch(WiFi.status()) {
        case WL_CONNECTED:
          ConfirmConnected(); // also calls MDNS.notifyAPChange() once localIP is valid
          break;
        case WL_IDLE_STATUS:
        case WL_DISCONNECTED: // I am still trying to connect
          if(ConnectionTO.JustCheck()) open_AP();
          break;
        default: // WiFi.begin failed
          open_AP();
        }
        break;
      default:
        break;
      } // switch(ConnStatus)

#if defined(DO_OTA) && DO_OTA
      ArduinoOTA.handle();
      // Stall watchdog: ArduinoOTA does not reliably fire onError when an upload
      // dies mid-transfer (uploading host vanished, TCP wedged), so OTA_IsInProgress
      // can get stuck true -- which silently freezes any sketch that gates its work
      // on it (no sampling, no logging, web server still alive). If no OTA progress
      // for OTA_StallTimeout_ms, assume the upload is dead and reboot to recover.
      if(OTA_IsInProgress && (millis() - OTA_LastActivity_ms > OTA_StallTimeout_ms)) {
        debug_puts("OTA stalled -- rebooting to recover");
        ESP.restart();
      }
#endif
#if defined(ESP8266)
      MDNS.update();
#endif
      // delay(1);
      yield();
    } // call_in_loop
  }; // struct StaticWiFi_Conn
} // namespace avp
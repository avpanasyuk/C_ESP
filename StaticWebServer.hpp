/**
 * @file StaticWebServer.h
 * @author
 * @brief going back to sync WebServer only, Async one does not have much use and too risky, as
 * all WebServer stuff happes in a different thread
 * @version 0.1
 * @date 2025-07-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <memory>
#include <Arduino.h>

#ifdef ESP32
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#elif defined(ESP8266)
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#include <ESP8266HTTPUpdateServer.h>
using HTTPUpdateServer = ESP8266HTTPUpdateServer;
#else
#error "Unsupported platform"
#endif

#include "C_ARDUINO/General.h"
#include "StaticWiFi_Conn.hpp"
#include "HTML_Log.hpp"

namespace avp {
  class avp::Print DebugPrint(debug_puts);
  // static void dot() { debug_puts("."); }

  /**
   * @brief This is interface class for static Sync WebServer
   */
  class StaticWebServer : public StaticWiFi_Conn {
  public:
    static inline ::WebServer s;

    struct HTTP {
      enum class Response_t : int {
        OK = 200,
        NOT_FOUND = 404,
        INTERNAL_SERVER_ERROR = 500,
        REDIRECT = 303
      };
    };

    static inline struct Options_t : public StaticWiFi_Conn::Options_t {
      const char *Version; ///< version of the board
      String AddUsage;     ///< additional commands in "Usage:" description657
      int LogSize;         ///< size of the log buffer
    } Options;             // Options_t

    static const Options_t &DefaultOpts() {
      static Options_t Opts;
      *(StaticWiFi_Conn::Options_t *)&Opts = StaticWiFi_Conn::DefaultOpts();
      Opts.Version = "0.0";
      Opts.AddUsage = "";
      Opts.LogSize = 2000;
      return Opts;
    } // DefaultOpts

    static void call_in_loop() {
      StaticWiFi_Conn::call_in_loop();
      s.handleClient();
      if(Update.hasError()) {
        StreamString str;
        Update.printError(str);
        debug_puts(str.c_str());
      }
    };

    //---------------- server functions -----------------------------
    static void send(const char *contentType, const String &content,
      HTTP::Response_t code = HTTP::Response_t::OK) {
      s.send(int(code), contentType, content);
    }
    static void send_P(const char *content_type, const uint8_t *content,
      size_t contentLength, HTTP::Response_t code = HTTP::Response_t::OK) {
      s.send_P(int(code), content_type, (const char *)content, contentLength);
    }
    // static bool hasArg(const String &name) { return s.hasArg(name); }
    // static int args() { return s.args(); }
    // static const String arg(const String &name) { return s.arg(name); };
    // static void sendHeader(const String &name, const String &value, bool first = false) {
    //   s.sendHeader(name, value, first);
    // }
    // static HTTPUpload &upload() { return s.upload(); }
    // static WiFiClient client() { return s.client(); }
    // static void sendContent(const String &content) { s.sendContent(content); }

    // ----------------- callbacks registration ---------------------------
    static inline void on(const Uri &uri, WebServer::THandlerFunction fn, HTTPMethod method = HTTP_GET) {
      s.on(uri, method, fn);
    }

    static inline void on(const Uri &uri, WebServer::THandlerFunction fn, WebServer::THandlerFunction ufn, HTTPMethod method = HTTP_PUT) {
      s.on(uri, method, fn, ufn);
    }

    static void begin(const Options_t &Opts = DefaultOpts()) {
      Options = Opts;
      HTML_Log::begin(Options.LogSize);
      StaticWiFi_Conn::begin(Opts);

      on("/", []() {
        static String Resp;
        Resp.reserve(200);
        Resp = "<!DOCTYPE HTML>\r\n<html>";
        Resp += Greeting();
        Resp += ", Version: ";
        Resp += Options.Version;
        Resp += F("<br><p><strong>Usage:</strong><br>"
                  "Available URL commands are (used as <b>http://");
        Resp += Name;
        Resp += F("/</b><em>command</em>):<ol>"
                  "<li> <em>nothing</em> - outputs this screen</li>"
                  "<li> <em>pin?i=n</em> - return pin n settings</li>"
                  "<li> <em>pin?i=n[&set=(0|1)]</em> - set pin value</li>"
                  "<li> <em>pin?i=n[&mode=(0|1)]</em> - set pin mode</li>"
                  "<li> <em>config?ssid=</em>......<em>&pass=</em>......</li>"
                  "<li> <em>log</em> - outputs debug log</li>"
                  "<li> <em>update</em> - update firmware</li>"
                  "<li> <em>reset</em> - reboots MCU</li>");
        Resp += Options.AddUsage;
        Resp += ShowWiFiAndEntry();
        Resp += "</html>";
        send("text/html", Resp);
      });

      on("/config", []() { // URL xxx.xxx.xxx.xxx/set?pin=14&value=1
        String qsid = s.arg("ssid");
        String qpass = s.arg("pass");
        if(qsid.length() > 0 && qpass.length() > 0) {
          send("text/plain", "WiFI configuration changed, connection is being reistablished!");
          delay(1000);
          WiFi.disconnect();
          delay(1000);
          scanNetworks(false, qsid.c_str());
          BeginConnectingToBestAP(qsid.c_str(), qpass.c_str());
          if(WiFi.waitForConnectResult() == WL_CONNECTED) StoreAUTH(qsid.c_str(), qpass.c_str());
          // delay(1000); server is still running, no point to reset
          // ESP.restart();
        }
      });

      on("/pin", []() { // URL xxx.xxx.xxx.xxx/pin?i=n[&analog][&set=x][&mode=x]
        if(s.hasArg("i")) {
          uint8_t Pin = s.arg("i").toInt();
          bool Analog = s.hasArg("analog");
          if(s.hasArg("set") || s.hasArg("mode")) {
            if(s.hasArg("mode")) {
              pinMode(Pin, s.arg("mode").toInt());
              send("text/plain", "Pin mode is set!");
            }
            if(s.hasArg("set")) {
              if(Analog) analogWrite(Pin, s.arg("set").toInt());
              else digitalWrite(Pin, s.arg("set").toInt());
              send("text/plain", "Pin is set!");
            }
          } else {
            if(Analog) send("text/plain", String("Analog pin #") + Pin + " reads " + analogRead(Pin));
            else send("text/plain", String("Digital pin #") + Pin + " reads " + digitalRead(Pin));
          }
        } else send("text/plain", "No pin index!");
      });

      on("/log", []() {
        send("text/html", avp::GenerateHTML(HTML_Log::Get(), 2, "LOG"));
      });

      on("/reset", []() {
        send("text/plain", "Resetting ...");
        delay(1000);
        ESP.restart();
      });

      static HTTPUpdateServer httpUpdater;

#ifdef ESP32
      httpUpdater.setup(&s);
#else
      httpUpdater.setup(&s, "/update");
#endif

      s.begin();
    } // begin

    static const String &ShowWiFiAndEntry() {
      static String Resp;
      Resp.reserve(200);

      Resp = F("</ol></p><p><b>WiFi networks:</b></p>");
      Resp += "<p>";
      Resp += scan();
      Resp += F("</p><form method='get' action='/config'><label>SSID: </label><input name='ssid' maxlength=");
      Resp += STR_SIZE - 1;
      Resp += " value='";
      Resp += WiFi.SSID();
      Resp += "'><input name='pass' maxlength=";
      Resp += STR_SIZE - 1;
      Resp += "><input type='submit'>";

      return Resp;
    } // ShowWiFiAndEntry
  }; // class StaticWebServer
} // namespace avp

/**
 * @file WebServer.h
 * @author
 * @brief this is interface class for both sync and async WebServers.
 * @version 0.1
 * @date 2025-07-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <functional>
#include <memory>
#include <Arduino.h>
#include "WiFi_Connection.h"

namespace avp {
  /**
   * @brief This is interface class for Sync and Async Web servers
   * Only one implementation file - either SyncWebServer.cpp or AsyncWebServer.cpp
   * should be compiled. To create an object use generator function "Create",
   * then "TryToConnect" until WiFi.isConnected(), "begin" and "call_in_loop"
   */
  class WebServer : public WiFi_Connection {
    std::shared_ptr<avp::Log> pLog;

  public:
    struct HTTP {
      enum class Method_t : uint8_t {
        GET = 0b00000001,
        POST = 0b00000010,
        DELETE = 0b00000100,
        PUT = 0b00001000,
        PATCH = 0b00010000,
        HEAD = 0b00100000,
        OPTIONS = 0b01000000,
        ANY = 0b01111111,
      };

      enum class Response_t : int {
        OK = 200,
        NOT_FOUND = 404,
        INTERNAL_SERVER_ERROR = 500
      };
    };

    /**
     * @brief this is a service class. It pretends to be either ::WebServer for sync server or
     * AsyncWebServerRequest for async to call all the member functions below
     */
    class Request_t {
    public:
      virtual bool hasArg(const String &name) const = 0;
      virtual int args() const = 0;
      virtual const String arg(const String &name) const = 0;
      virtual void send(const char *contentType = "", const String &content = emptyString,
        HTTP::Response_t code = HTTP::Response_t::OK) = 0;
      virtual void send_P(const char *content_type, const uint8_t *content,
        size_t contentLength, HTTP::Response_t code = HTTP::Response_t::OK) = 0;
      virtual void sendHeader(const String &name, const String &value, bool first = false) = 0;
    }; // class Request_t

    using RequestHandler_t = ::std::function<void(Request_t &&rReq, const char *uri, HTTP::Method_t method)>;
    using RH_Simple_t = ::std::function<void(Request_t &&rReq)>;
    // typedef void RequestHandler_t(Request_t &&rReq, const char *uri, HTTP::Method_t method);
    // typedef void RH_Simple_t(Request_t &&rReq);

    struct Options_t : public WiFi_Connection::Options_t {
      const char *Version; ///< version of the board
      String AddUsage;     ///< additional commands in "Usage:" description657
      int LogSize;         ///< size of the log buffer
    } Options;             // Options_t

    static Options_t DefaultOpts() { return {{WiFi_Connection::DefaultOpts()}, "0.0", "", 2000}; }

    explicit WebServer(const Options_t &Opts = DefaultOpts()) : WiFi_Connection(Opts), Options(Opts) {
      pLog = std::make_shared<avp::Log>(Options.LogSize);
    } // constructor

    virtual void call_in_loop() { WiFi_Connection::call_in_loop(); };

    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method = HTTP::Method_t::GET) = 0;
    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method = HTTP::Method_t::GET) = 0;
    virtual void on(const char *uri, RH_Simple_t handler, RH_Simple_t upload_handler, HTTP::Method_t method = HTTP::Method_t::POST) = 0;
    virtual void AddToLog(const char *s, bool NoBreak = false) { pLog->Add(s, NoBreak); } // AddToLog

    virtual void begin() {
      on("/", [this](Request_t &&rReq) {
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
                  "<li> nothing - outputs this screen</li>"
                  "<li> pin?i=n - return pin n settings</li>"
                  "<li> pin?i=n[&set=(0|1)] - set pin value</li>"
                  "<li> pin?i=n[&mode=(0|1)] - set pin mode</li>"
                  "<li> config?ssid=<em>string</em>&pass=<em>string</em></li>"
                  "<li> log - outputs debug log</li>"
                  "<li> update - update firmware</li>"
                  "<li> reset - reboots MCU</li>");
        Resp += Options.AddUsage;
        Resp += ShowWiFiAndEntry();
        Resp += "</html>";
        rReq.send("text/html", Resp);
      });

      on("/config", [this](Request_t &&rReq) { // URL xxx.xxx.xxx.xxx/set?pin=14&value=1
        String qsid = rReq.arg("ssid");
        String qpass = rReq.arg("pass");
        if(qsid.length() > 0 && qpass.length() > 0) {
          rReq.send("text/plain", "WiFI configuration changed, connection is being reistablished!");
          delay(1000);
          WiFi.disconnect();
          delay(1000);
          ConnectToBestAP(qsid.c_str(), qpass.c_str());
          if(WiFi.waitForConnectResult() == WL_CONNECTED) StoreAUTH(qsid.c_str(), qpass.c_str());
          // delay(1000); server is still running, no point to reset
          // ESP.restart();
        }
      });

      on("/pin", [](Request_t &&rReq) { // URL xxx.xxx.xxx.xxx/pin?i=n[&analog][&set=x][&mode=x]
        if(rReq.hasArg("i")) {
          uint8_t Pin = rReq.arg("i").toInt();
          bool Analog = rReq.hasArg("analog");
          if(rReq.hasArg("set") || rReq.hasArg("mode")) {
            if(rReq.hasArg("mode")) {
              pinMode(Pin, rReq.arg("mode").toInt());
              rReq.send("text/plain", "Pin mode is set!");
            }
            if(rReq.hasArg("set")) {
              if(Analog) analogWrite(Pin, rReq.arg("set").toInt());
              else digitalWrite(Pin, rReq.arg("set").toInt());
              rReq.send("text/plain", "Pin is set!");
            }
          } else {
            if(Analog) rReq.send("text/plain", String("Analog pin #") + Pin + " reads " + analogRead(Pin));
            else rReq.send("text/plain", String("Digital pin #") + Pin + " reads " + digitalRead(Pin));
          }
        } else rReq.send("text/plain", "No pin index!");
      });

      on("/log", [this](Request_t &&rReq) {
        rReq.send("text/html", avp::GenerateHTML(pLog->Get(), 2, "LOG"));
      });

      on("/reset", [](Request_t &&rReq) {
        rReq.send("text/plain", "Resetting ...");
        delay(1000);
        ESP.restart();
      });
    } // begin

    static ::std::unique_ptr<WebServer> Create(const Options_t &Opts = DefaultOpts(), uint16_t port = 80);

    static const String &ShowWiFiAndEntry() {
      static String Resp;
      Resp.reserve(200);

      Resp = F("</ol></p><p><b>WiFi networks:</b></p>");
      Resp += "<p>";
      Resp += scan();
      Resp += F("</p><form method='get' action='/config'><label>SSID: </label><input name='ssid' length=");
      Resp += STR_SIZE - 1;
      Resp += " value='";
      Resp += WiFi.SSID();
      Resp += "'><input name='pass' length=";
      Resp += STR_SIZE - 1;
      Resp += "><input type='submit'></html>";

      return Resp;
    } // ShowWiFiAndEntry

  }; // class WebServer
} // namespace avp

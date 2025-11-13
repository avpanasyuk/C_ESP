/**
 * @file AAsyncWebServer.cpp
 * @author your name (you@domain.com)
 * @brief This file is implementation part of Server class with SyncServer.
 * I made two separate cpp files for SyncServer and AsyncServer because I do not
 * want to include both serts of include files which can bring two libraries
 * @version 0.1
 * @date 2025-11-12
 *
 * @copyright Copyright (c) 2025
 *
 */
#include <cstdint>

#ifdef ESP32
#include <AsyncTCP.h>
#include <WiFi.h>

#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#else
#error "Unsupported platform"
#endif

#include <ESPAsyncWebServer.h>
using WebServer = AsyncWebServer;

#include "WebServer.h"

namespace avp {
  class AsyncWebServer : public avp::WebServer, ::WebServer {
  
    class Request_t : public avp::WebServer::Request_t {
      AsyncWebServerRequest *p;

    public:
      Request_t(AsyncWebServerRequest *p_) : p(p_) {}

      virtual bool hasArg(const String &name) const override { return p->hasArg(name); }
      virtual int args() const override { return p->args(); }
      virtual const String& arg(const String& name) const override { return p->arg(name); }; 
      virtual void send(HTTP::Response_t code, const char *contentType, const String &content) override {
        p->send(int(code),contentType,content);
      };
    }; // class Request_t

  public:
    explicit AsyncWebServer(uint16_t port) : WebServer(), ::WebServer(port) {}
    virtual void begin() override { ::WebServer::begin(); }

    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
      ::WebServer::on(uri, uint8_t(method), [&](AsyncWebServerRequest *request) {
        Request_t r(request);
        handler(&r, uri, method);
        return ::AsyncCallbackWebHandler();
      });
    }

    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
      ::WebServer::on(uri, uint8_t(method), [&](AsyncWebServerRequest *request) {
        Request_t r(request);
        handler(&r);
        return ::AsyncCallbackWebHandler();
      });
    }
  }; // class WebServer

  ::std::unique_ptr<WebServer> WebServer::Create(uint16_t port) {
    return ::std::make_unique<AsyncWebServer>(port);
  }
} // namespace avp


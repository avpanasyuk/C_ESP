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

#include <Update.h>
#include "WebServer.h"

namespace avp {
  class AsyncWebServer : public avp::WebServer, ::WebServer {

    class Request_t : public avp::WebServer::Request_t {
      AsyncWebServerRequest *const p;

    public:
      Request_t(AsyncWebServerRequest *p_) : p(p_) { AVP_ASSERT(p != nullptr); }

      virtual bool hasArg(const String &name) const override { return p->hasArg(name); }
      virtual int args() const override { return p->args(); }
      virtual const String arg(const String &name) const override { return p->arg(name); };
      virtual void send(const char *contentType, const String &content, HTTP::Response_t code) override {
        p->send(int(code), contentType, content);
      };
      virtual void send_P(const char *content_type, const uint8_t *content,
        size_t contentLength, HTTP::Response_t code = HTTP::Response_t::OK) {
        p->send_P(int(code), content_type, (const char *)content, contentLength);
      }
      virtual void sendHeader(const String &name, const String &value, bool first = false) {
        p->sendHeader(name, value, first);
      }
      virtual void upload(WebServerType &server, const String &requestUri, HTTPUpload &upload) {
        (void)server;
        (void)requestUri;
        (void)upload;
      }
    }; // class Request_t

  public:
    explicit AsyncWebServer(uint16_t port) : WebServer(), ::WebServer(port) {}
    virtual void begin() override {
      ::WebServer::begin();
      WebServer()::begin();
    }
    virtual void call_in_loop() override { WebServer::call_in_loop(); };

    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method) override {
      ::WebServer::on(uri, uint8_t(method), [handler, uri, method](AsyncWebServerRequest *request) {
        handler(Request_t(request), uri, method);
        return ::AsyncCallbackWebHandler();
      });
    }

    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method) override {
      ::WebServer::on(uri, uint8_t(method), [handler](AsyncWebServerRequest *request) {
        handler(Request_t(request));
        return ::AsyncCallbackWebHandler();
      });
    }

    virtual void on(const char *uri, RH_Simple_t handler, RH_Simple_t upload_handler, HTTP::Method_t method) override {
      ::WebServer::on(uri, uint8_t(method), [handler](AsyncWebServerRequest *request) {
        handler(Request_t(request));
        return ::AsyncCallbackWebHandler();
      },
        [upload_handler](AsyncWebServerRequest *request) {
          upload_handler(Request_t(request));
          return ::AsyncCallbackWebHandler();
        });
    }

  }; // class WebServer

  ::std::unique_ptr<WebServer> WebServer::Create(uint16_t port) {
    return ::std::make_unique<AsyncWebServer>(port);
  }
} // namespace avp

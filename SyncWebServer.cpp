/**
 * @file SyncWebServer.cpp
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
#include <WebServer.h>
#elif defined(ESP8266)
#include <ESP8266WebServer.h>
using WebServer = ESP8266WebServer;
#else
#error "Unsupported platform"
#endif

#include "WebServer.h"

namespace avp {
  class SyncWebServer : public avp::WebServer, ::WebServer {
    // ::WebServer server;

    static HTTPMethod ConvertMethod(HTTP::Method_t m) {
      switch(m) {
      case HTTP::Method_t::GET:
        return HTTPMethod::HTTP_GET;
      case HTTP::Method_t::POST:
        return HTTPMethod::HTTP_POST;
      case HTTP::Method_t::DELETE:
        return HTTPMethod::HTTP_DELETE;
      case HTTP::Method_t::PUT:
        return HTTPMethod::HTTP_PUT;
      case HTTP::Method_t::PATCH:
        return HTTPMethod::HTTP_PATCH;
      case HTTP::Method_t::HEAD:
        return HTTPMethod::HTTP_HEAD;
      case HTTP::Method_t::OPTIONS:
        return HTTPMethod::HTTP_OPTIONS;
      default:
      case HTTP::Method_t::ANY:
        return HTTPMethod::HTTP_ANY;
      }
    } // ConvertMethod
    
    class Request_t : public avp::WebServer::Request_t {
      ::WebServer *p;

    public:
      Request_t(::WebServer *p_) : p(p_) {}

      virtual bool hasArg(const String &name) const override { return p->hasArg(name); }
      virtual int args() const override { return p->args(); }
      virtual const String& arg(const String& name) const override { return p->arg(name); }; 
      virtual void send(HTTP::Response_t code, const char *contentType, const String &content) override {
        p->send(int(code),contentType,content);
      };
    }; // class Request_t

  public:
    explicit SyncWebServer(uint16_t port) : WebServer(), ::WebServer(port) {}
    virtual void begin() override { ::WebServer::begin(); }
      

    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
      ::WebServer::on(uri, ConvertMethod(method), [&]() {
        Request_t r(this);
        handler(&r, uri, method);
      });
    }

    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
      ::WebServer::on(uri, ConvertMethod(method), [&]() {
        Request_t r(this);
        handler(&r);
      });
    }
  }; // class WebServer

::std::unique_ptr<WebServer> WebServer::Create(uint16_t port) {
    return ::std::make_unique<SyncWebServer>(port);
  }

} // namespace avp

/**
 * @file WebServer.h
 * @author
 * @brief this is interface class for both sync and async WebServers.
 * Only one implementation file - either SyncWebServer.cpp or AsyncWebServer.cpp
 * should be compiled
 * @version 0.1
 * @date 2025-07-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <functional>
#include <memory>

// #ifdef ESP32
// #include <WebServer.h>
// #elif defined(ESP8266)
// #include <ESP8266WebServer.h>
// #else
// #error "Unsupported platform"
// #endif

#include "WiFi_Connection.h"

namespace avp {
  /**
   * @brief This is interface class for Sync and Async Web servers
   *
   */
  class WebServer : public WiFi_Connection {
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
        NOT_FOUND = 404
      };
    };

    /**
     * @brief this class pretends to be either ::WebServer * for sync server or AsyncWebServerRequest * for async
     * because you need this pointer to call all the member functions
     */
    class Request_t {
      public:
      virtual bool hasArg(const String &name) const = 0;
      virtual int args() const = 0;
      virtual const String& arg(const String& name) const = 0;
      virtual void send(HTTP::Response_t code, const char *contentType = "", const String &content = emptyString) = 0;
    };

    // using SendFunction_t = ::std::function<void(HTTP::Response_t code, const char *contentType, const char *content)>;
    using RequestHandler_t = ::std::function<void(Request_t *pReq, const char *uri, HTTP::Method_t method)>;
    using RH_Simple_t = ::std::function<void(Request_t *pReq)>;

    struct Options_t : public WiFi_Connection::Options_t {
      const char *Version; //< version of the board
      String AddUsage;     //< additional commands in "Usage:" description657
      int LogSize;
    } Options; // Options_t

    static Options_t DefaultOpts() { return {{WiFi_Connection::DefaultOpts()}, "0.0", "", 2000}; }

    explicit WebServer(const Options_t &Opts = DefaultOpts()) : WiFi_Connection(Opts), Options(Opts) {
    } // constructor

    virtual void begin() = 0;
    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method = HTTP::Method_t::GET) = 0;
    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method = HTTP::Method_t::GET) = 0;

    static ::std::unique_ptr<WebServer> Create(uint16_t port);

  }; // class Server
} // namespace avp

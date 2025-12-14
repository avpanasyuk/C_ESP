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

#include "C_General/Error.hpp"
#include "C_General/MyTime.hpp"
#include "C_ARDUINO/General.h"
#include "WebServer.h"

class avp::Print DebugPrint(debug_puts);
static void dot() { debug_puts("."); }
namespace avp {
  class SyncWebServer : public avp::WebServer, ::WebServer {
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
        return HTTP_ANY;
      }
    } // ConvertMethod

    class Request_t : public avp::WebServer::Request_t {
      ::WebServer *const p;

    public:
      Request_t(::WebServer *p_) : p(p_) { AVP_ASSERT(p != nullptr); }

      virtual bool hasArg(const String &name) const override { return p->hasArg(name); }
      virtual int args() const override { return p->args(); }
      virtual const String arg(const String &name) const override { return p->arg(name); };
      virtual void send(const char *contentType, const String &content,
        HTTP::Response_t code = HTTP::Response_t::OK) {
        p->send(int(code), contentType, content);
      }
      virtual void send_P(const char *content_type, const uint8_t *content,
        size_t contentLength, HTTP::Response_t code = HTTP::Response_t::OK) {
        p->send_P(int(code), content_type, (const char *)content, contentLength);
      }
      virtual void sendHeader(const String &name, const String &value, bool first = false) {
        p->sendHeader(name, value, first);
      }
      virtual HTTPUpload &upload() { return p->upload(); }
      virtual WiFiClient client() const { return p->client(); }
      virtual void sendContent(const String &content) { p->sendContent(content); }
    }; // class Request_t

  public:
    explicit SyncWebServer(const Options_t &Opts, uint16_t port) : avp::WebServer(Opts), ::WebServer(port) {}
    virtual void begin() {
      ::WebServer::begin();
      avp::WebServer::begin();
      on("/update", [](avp::WebServer::Request_t &&rReq) {
        rReq.send("text/html",
          F("<!DOCTYPE html><html><head>"
            "<title>ESP OTA Update</title>"
            "</head><body>"
            "<h1>ESP Firmware Update</h1>"
            "<p>Upload new firmware (.bin file):</p>"
            "<form method='POST' action='/upload' enctype='multipart/form-data'>"
            "<input type='file' name='update' accept='.bin'>"
            "<input type='submit' value='Update'>"
            "</form></body></html>"));
      });

      on("/upload", [](avp::WebServer::Request_t &&rReq) {
        // auto &syncReq = static_cast<SyncWebServer::Request_t &>(rReq);
        // syncReq.sendHeader("Connection", "close");
        rReq.send("text/plain", (Update.hasError()) ? "FAIL" : "OK");
      },
        [](avp::WebServer::Request_t &&rReq) {
        auto &syncReq = static_cast<SyncWebServer::Request_t &>(rReq);
        HTTPUpload &upload = syncReq.upload();

        if(upload.status == UPLOAD_FILE_START) {
          debug_printf("Update: %s\n", upload.filename.c_str());
#ifdef ESP8266
          WiFiUDP::stopAll();
          if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
#endif
#ifdef ESP32
            if(!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#endif
              Update.printError(DebugPrint);
            }
          } else if(upload.status == UPLOAD_FILE_WRITE) {
            if(Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
              Update.printError(DebugPrint);
            }
          } else if(upload.status == UPLOAD_FILE_END) {
            if(Update.end(true)) {
              debug_printf("Update Success: %u bytes\n", upload.totalSize);
              rReq.send("text/plain", "Update successful! Device rebooting.");
              delay(1000);
              ESP.restart();
            } else {
              Update.printError(DebugPrint);
              rReq.send("text/plain", "Update failed.", HTTP::Response_t::INTERNAL_SERVER_ERROR);
            }
          } else if(upload.status == UPLOAD_FILE_ABORTED) {
            Update.end();
            rReq.send("text/plain", "Update aborted.", HTTP::Response_t::INTERNAL_SERVER_ERROR);
          }
        });
    }

    virtual void StopServer() override {
        ::WebServer::stop(); }

    virtual void call_in_loop() override {
        avp::WebServer::call_in_loop();
        ::WebServer::handleClient();
#if defined(DEBUG) && DEBUG
        avp::Periodically<dot>::Run(1000);
#endif
    } // call_in_loop

    virtual void on(const char *uri, RequestHandler_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
        ::WebServer::on(uri, ConvertMethod(method), [this, handler, uri, method]() { // do not capture by reference, stack disappears
          handler(Request_t(this), uri, method);
        });
    }

    virtual void on(const char *uri, RH_Simple_t handler, HTTP::Method_t method = HTTP::Method_t::GET) override {
        ::WebServer::on(uri, ConvertMethod(method), [this, handler]() {
          handler(Request_t(this));
        });
    }

    virtual void on(const char *uri, RH_Simple_t handler, RH_Simple_t upload_handler, HTTP::Method_t method = HTTP::Method_t::POST) override {
        ::WebServer::on(uri, ConvertMethod(method), [this, handler]() {
          handler(Request_t(this));
        },
          [this, upload_handler]() {
            upload_handler(Request_t(this));
          });
    }
    }; // class WebServer
  } // namespace avp

  ::std::unique_ptr<avp::WebServer>
  avp::WebServer::Create(const Options_t &Opts, uint16_t port) {
    return ::std::make_unique<avp::SyncWebServer>(Opts, port);
  }

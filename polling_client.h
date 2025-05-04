#pragma once

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <WiFiClientSecure.h>

namespace avp {
  class Client_ {
    WiFiClient &Client;
    IPAddress remote_addr;
    const char *server;
    uint16_t port;
    unsigned long Timeout_ms, EndTimeout_ms;
    String GET_response;
    void (*call_back)(const char *);
    const char *Error;
  protected:
    static constexpr unsigned long DEFAULT_TIMEOUT_MS = 20000;
    void GetIP() { if(!WiFi.hostByName(server, remote_addr)) Error = "Can not resolve name"; }
  public:
    explicit Client_(WiFiClient &Client_, const char *server_, uint16_t port_, unsigned long Timeout_ms_) :
      Client(Client_), server(server_), port(port_), Timeout_ms(Timeout_ms_), call_back(nullptr),
      Error(nullptr) {
      Client.setTimeout(Timeout_ms);
      GetIP();
   } // constructor

    /**
     * @brief sends GET request. Sets Error if things go wrong
     *
     * @param Message
     * @param call_back_ is calles when response arrives. it receives only data part of response
     * @return const char* == nullptr if OK, or error message if not;
     */
    const char *SendGET(const char *Message, void (*call_back_)(const char *) = [](const char *) { });
    void loop();
    const char *GetError() const { return Error; }
    void ResetError() { Error = nullptr; }
  }; // class Client_

  class Client : public Client_ {
    WiFiClient c;
  public:
    explicit Client(const char *server, uint16_t port = 80,
      unsigned long Timeout_ms = Client_::DEFAULT_TIMEOUT_MS) :
      Client_(c, server, port, Timeout_ms) { };
  }; // class Client

  class Client_Secure : public Client_ {
    WiFiClientSecure c;
  public:
    explicit Client_Secure(const char *server, uint16_t port = 443,
      unsigned long Timeout_ms = Client::DEFAULT_TIMEOUT_MS) :
      Client_(c, server, port, Timeout_ms) {
      c.setInsecure();
    };
  }; // class Client_Secure  
} // namespace avp
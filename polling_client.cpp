#include "C_General\General.h"
#include "C_General\Error.h"
#include "polling_client.h"


namespace avp {
  const char *Client_::SendGET(const char *Message, void (*call_back_)(const char *)) {
    if(Error != nullptr) return Error;
    if(call_back_ == nullptr) return Error = "Callback can not be nullptr!";
    if(call_back != nullptr) return Error = "Previous request is pending!";

    GetIP();
    if(Error != nullptr) return Error;
    if(!Client.connect(remote_addr, port)) return Error = "Failed to connect to server!";
    Client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ff\r\nConnection: close\r\n\r\n", Message, server);

    // we are waiting until either response reaches or server closes connection
    EndTimeout_ms = millis() + Timeout_ms;
    GET_response.clear();
    call_back = call_back_;
    return nullptr;
  } // SendGET

  void Client_::loop() {
      if(GetError()) {
        debug_printf("%s:%hu client is in error '%s'\n", server, port, GetError());
        ResetError();
      }

      if(call_back != nullptr) {
        if(Client.connected() && avp::unsigned_is_smaller(millis(), EndTimeout_ms)) {
          if(Client.available()) GET_response += char(Client.read());
        } else {
#ifdef DEBUG
          debug_puts(GET_response.c_str());
#endif      
          Client.stop();

          static const char *headers_end = "\r\n\r\n";
          const char *response_message = strstr(GET_response.c_str(), headers_end);
          if(response_message != nullptr) call_back(response_message + strlen(headers_end));
          call_back = nullptr;
        }
      }
    } // loop
} // namespace avp
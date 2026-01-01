#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <Arduino.h>

namespace avp {
  // ***************** INTERNET CLIENT CONNECTION
  // ************************************
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s = 0, const char *title = nullptr);

  /**
   * @brief finds AP with best BSSID
   *
   * @param SSID
   * @param BSSID_out - array uint8_t[6] were result is stored
   * @return true - found at least one AP
   * @return false - did not find any APs
   */
  bool FindBestAP(const char *SSID, uint8_t *BSSID_out);

  /**
   * @brief Finds best AP and connects
   *
   * @param SSID
   * @param PWD
   * @return true is succeded
   */
  bool TryToConnectWiFi(const char *SSID, const char *PWD);

  /**
   * @brief to be used with csv_logging_server.py
   *
   * @param URL: like "http://192.168.1.100:8000/update"
   * @param s: csv string, first entry is the name of file to store
   * @return nullptr is succeded, error message if not.
   */
  const char *HTTP_POST_puts(const char *URL, const char *s);
} // namespace avp

// following is much fatser than Arduino "NoInterrupts".
#define PAUSE_ESP_INTERRUPTS                            \
  volatile struct _t {                                  \
    unsigned int state;                                 \
    _t() { asm volatile("rsil %0, 15" : "=r"(state)); } \
    ~_t() { asm volatile("wsr %0, ps" ::"r"(state)); }  \
  } _;

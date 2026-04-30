#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <Arduino.h>

namespace avp {
  // ***************** INTERNET CLIENT CONNECTION
  // ************************************
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s = 0, const char *title = nullptr);

  /**
   * @brief Calls WiFi.scanNetworks. To get number of detected APs use WiFi.scanComplete()
   *
   * @param Async
   * @param SSID if specified looking only for corresponding APs
   */
  void scanNetworks(bool Async, const char *SSID = nullptr);

  /**
   * @brief It is assumes that we ran either sync or async WiFi.scanNetworks and it is completed
   *
   * @param OutIndex Best RSSI index
   * @retval Error message, nullptr is not error
   */
  const char *FindTheBestAPinScan(uint8_t &BestRSSI_i);
    
    /**
   * @brief to be used with csv_logging_server.py
   *
   * @param URL: like "http://192.168.1.100:8000/update"
   * @param s: csv string, first entry is the name of file to store
   * @return nullptr is succeded, error message if not.
   */
  const char *HTTP_POST_puts(const char *URL, const char *s, size_t sz);
  const char *HTTP_POST_puts(const char *URL, const char *s);
  String BSSIDtoString(const uint8_t *BSSID);
  /**
   * @brief scans all available networks and returns a table
   *
   * @return const String& - HTML formatted table
   */
  const String &scan();
} // namespace avp

// following is much fatser than Arduino "NoInterrupts".
#define PAUSE_ESP_INTERRUPTS                            \
  volatile struct _t {                                  \
    unsigned int state;                                 \
    _t() { asm volatile("rsil %0, 15" : "=r"(state)); } \
    ~_t() { asm volatile("wsr %0, ps" ::"r"(state)); }  \
  } _;

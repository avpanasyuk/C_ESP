#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <Arduino.h>

namespace avp {
  // ***************** INTERNET CLIENT CONNECTION
  // ************************************
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s = 0, const char *title = nullptr);

  /**
   * @brief It is assumes that we ran either sync or async WiFi.scanNetworks and it is completed with
   *        some APs found
   *
   * @param BestRSSI_i: output Best AP index
   * @param Channel: output, if all APs are on the same channel its value
   * @retval true if success
   */
  bool FindTheBestAPinScan(uint8_t &BestRSSI_i, int32_t &Channel);

  /**
   * @brief Scan + select-strongest convenience wrapper for projects that don't
   *        use StaticWiFi_Conn. Blocks for the scan duration (~2 s).
   *
   * @param SSID      SSID to filter the scan to
   * @param BSSID_out 6-byte buffer; filled with the BSSID of the strongest matching AP
   * @retval true if at least one matching AP was found
   */
  bool FindBestAP(const char *SSID, uint8_t *BSSID_out);


    /**
   * @brief to be used with csv_logging_server.py
   *
   * @param URL: like "http://bsd:8000/"
   * @param s: csv string, first entry is the name of file to store
   * @return true on success; on failure returns false and logs the cause itself
   *         to the /log (HTML_Log) buffer -- it posts nothing, so do NOT re-log
   *         it through debug_puts.
   */
  bool HTTP_POST_puts(const char *URL, const char *s, size_t sz);
  bool HTTP_POST_puts(const char *URL, const char *s);
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

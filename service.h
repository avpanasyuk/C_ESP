#pragma once

#include <Arduino.h>
#include <stdint.h>

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

  } // namespace avp

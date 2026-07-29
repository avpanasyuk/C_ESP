#pragma once
/**
 * @file fast_wake.hpp
 * @brief RTC-memory cache of WiFi association state for low-power ESP8266/ESP32
 *        projects that deep-sleep between work cycles.
 *
 * Use:
 *   - On a "Deep-Sleep Wake" reset, call ReadFastWakeWiFi() to recover the
 *     last-known BSSID/channel/IP. If it returns true, configure WiFi with
 *     WiFi.config(local_ip, gateway, subnet, dns1) and then
 *     WiFi.begin(SSID, PWD, channel, bssid, true). Association typically
 *     completes in <500 ms instead of 2-5 s, and DHCP is skipped.
 *   - After WiFi.status() == WL_CONNECTED on the slow path, call SnapshotWiFi()
 *     and WriteFastWakeWiFi() so the next wake can use the fast path.
 *   - If the fast-path begin() fails to associate, fall back to the slow path
 *     (FindBestAP + DHCP) and overwrite the cache on success.
 *
 * Storage: RTC memory survives deep-sleep but is lost on power-cycle / hard reset.
 * On ESP8266 it is the rtcUserMemory block addressed by a dword offset (default 0;
 * pass an offset past any other RTC structures the project keeps). On ESP32 it is a
 * single RTC_DATA_ATTR backing store (one cache per device) and the offset is ignored.
 *
 * CRC32 (Ethernet polynomial) validates the read; a power-cycled chip with
 * garbage in RTC memory will fail the check and fall back to slow boot.
 */

#include <Arduino.h>
#include <cstddef> // offsetof
#include <cstring> // memcpy
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "C_General/General.h" // ::Crc32

namespace avp {

  struct FastWakeWiFi {
    uint8_t  bssid[6];
    uint8_t  channel;
    uint8_t  reserved; // alignment padding; keep zero
    uint32_t local_ip;
    uint32_t gateway;
    uint32_t subnet;
    uint32_t dns1;
    uint32_t crc; // must be the last field; CRC covers everything above it
  };
  static_assert(sizeof(FastWakeWiFi) % 4 == 0, "RTC writes are dword-aligned");

  /// Read FastWakeWiFi from RTC memory (dword offset is ESP8266-only, ignored on
  /// ESP32). Returns true iff the read succeeded and the CRC matches (cache valid).
#ifdef ESP8266
  inline bool ReadFastWakeWiFi(FastWakeWiFi &out, uint32_t rtc_off_dwords = 0) {
    if(!ESP.rtcUserMemoryRead(rtc_off_dwords, (uint32_t *)&out, sizeof(out))) return false;
    return out.crc == ::Crc32((const uint8_t *)&out, offsetof(FastWakeWiFi, crc), 0xFFFFFFFFu, 0xEDB88320u);
  }

  /// Compute the CRC and write to RTC memory at the given dword offset.
  /// Mutates `in.crc`. Safe to call from any normal context.
  inline void WriteFastWakeWiFi(FastWakeWiFi &in, uint32_t rtc_off_dwords = 0) {
    in.crc = ::Crc32((const uint8_t *)&in, offsetof(FastWakeWiFi, crc), 0xFFFFFFFFu, 0xEDB88320u);
    ESP.rtcUserMemoryWrite(rtc_off_dwords, (uint32_t *)&in, sizeof(in));
  }

  /// Force the next ReadFastWakeWiFi() to fail, so the caller falls back to a full scan
  /// + fresh DHCP. Needed because a stale cache still *associates* -- the failure only
  /// shows up later as unreachable hosts (a lease reassigned while the device slept, an
  /// AP that moved), and nothing else invalidates it. Deliberately stores a deliberately
  /// wrong CRC rather than zeros, whose CRC is a valid value like any other.
  inline void InvalidateFastWakeWiFi(uint32_t rtc_off_dwords = 0) {
    FastWakeWiFi bad{};
    bad.crc = ~::Crc32((const uint8_t *)&bad, offsetof(FastWakeWiFi, crc), 0xFFFFFFFFu, 0xEDB88320u);
    ESP.rtcUserMemoryWrite(rtc_off_dwords, (uint32_t *)&bad, sizeof(bad));
  }
#elif defined(ESP32)
  // ESP32 has no rtcUserMemory API; a single RTC_DATA_ATTR backing store survives deep
  // sleep and is re-initialised to zero (-> CRC fails -> slow boot) on power-on / hard
  // reset. One cache per device; the offset arg is kept for API parity but ignored.
  inline RTC_DATA_ATTR FastWakeWiFi fastWakeStore;
  inline bool ReadFastWakeWiFi(FastWakeWiFi &out, uint32_t = 0) {
    out = fastWakeStore;
    return out.crc == ::Crc32((const uint8_t *)&out, offsetof(FastWakeWiFi, crc), 0xFFFFFFFFu, 0xEDB88320u);
  }
  inline void WriteFastWakeWiFi(FastWakeWiFi &in, uint32_t = 0) {
    in.crc = ::Crc32((const uint8_t *)&in, offsetof(FastWakeWiFi, crc), 0xFFFFFFFFu, 0xEDB88320u);
    fastWakeStore = in;
  }
#endif

  /// Capture the current WiFi STA association into `out`. Call after
  /// WiFi.status() == WL_CONNECTED on the slow path. Does NOT write the CRC --
  /// pair with WriteFastWakeWiFi(out, ...).
  inline void SnapshotWiFi(FastWakeWiFi &out) {
    out.reserved = 0;
    if(const uint8_t *b = WiFi.BSSID()) memcpy(out.bssid, b, 6);
    else memset(out.bssid, 0, 6);
    out.channel  = WiFi.channel();
    out.local_ip = (uint32_t)WiFi.localIP();
    out.gateway  = (uint32_t)WiFi.gatewayIP();
    out.subnet   = (uint32_t)WiFi.subnetMask();
    out.dns1     = (uint32_t)WiFi.dnsIP(0);
  }

} // namespace avp

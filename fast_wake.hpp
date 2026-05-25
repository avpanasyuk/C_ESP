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
 * Storage: RTC user memory survives deep-sleep but is lost on power-cycle / hard
 * reset. Caller picks the dword offset (default 0). For projects that already
 * use RTC memory for other state, pass an offset past their existing structures.
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

  inline uint32_t fastwake_crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    while(len--) {
      c ^= *p++;
      for(int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return ~c;
  }

  /// Read FastWakeWiFi from RTC user memory at the given dword offset. Returns
  /// true iff the read succeeded and the CRC matches (i.e. the cache is valid).
  inline bool ReadFastWakeWiFi(FastWakeWiFi &out, uint32_t rtc_off_dwords = 0) {
    if(!ESP.rtcUserMemoryRead(rtc_off_dwords, (uint32_t *)&out, sizeof(out))) return false;
    return out.crc == fastwake_crc32(&out, offsetof(FastWakeWiFi, crc));
  }

  /// Compute the CRC and write to RTC user memory at the given dword offset.
  /// Mutates `in.crc`. Safe to call from any normal context.
  inline void WriteFastWakeWiFi(FastWakeWiFi &in, uint32_t rtc_off_dwords = 0) {
    in.crc = fastwake_crc32(&in, offsetof(FastWakeWiFi, crc));
    ESP.rtcUserMemoryWrite(rtc_off_dwords, (uint32_t *)&in, sizeof(in));
  }

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

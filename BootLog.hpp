/**
 * @file BootLog.hpp
 * @brief Device lifecycle records (BOOT + SLEEP) for the fleet debug log.
 *
 * avp::LogBoot() / avp::LogSleep() emit a tidy CSV row via debug_printf (so it
 * reaches whatever sink the project wired -- e.g. FleetServerDebug -> Debug_log.csv):
 *
 *   BOOT,fw=<version>,rev=<rev>,reason=<reset-reason>,rssi=<dBm>[,vcc=<mV>][,vlow=<mV>][,<extra>]
 *   SLEEP,for=<seconds|until-wake>[,<extra>]
 *
 * LogSleep() is the bookend to LogBoot(): call it right before deep sleep, so a BOOT
 * with no following SLEEP flags a device that crashed mid-cycle, and the duration
 * tells a monitor when to expect the next wake.
 *
 * Call them AFTER the network sink can ship (e.g. after WiFi is up); a debug sink that
 * drops pre-connect output would otherwise discard them. `extra` carries project-
 * specific fields (e.g. "wake=60min") and may be nullptr.
 *
 * The reason uses the ESP8266 getResetReason() vocabulary; the ESP32 path maps
 * esp_reset_reason() onto the same strings so fleet logs read consistently.
 */
#pragma once
#include <Arduino.h>

#if defined(ESP8266)
#include <Esp.h>
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <esp_system.h>
#include <WiFi.h>
#endif

#include "C_General/Error.hpp" // debug_printf (overridable)

namespace avp {
#if defined(ESP32)
  // Map ESP32's reset-reason enum onto the ESP8266 getResetReason() strings so
  // the fleet log reads the same across both platforms.
  inline const char *ResetReasonStr(esp_reset_reason_t r) {
    switch(r) {
      case ESP_RST_POWERON:   return "Power On";
      case ESP_RST_EXT:       return "External System";
      case ESP_RST_SW:        return "Software/System restart";
      case ESP_RST_PANIC:     return "Exception";
      case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
      case ESP_RST_TASK_WDT:  return "Task Watchdog";
      case ESP_RST_WDT:       return "Watchdog";
      case ESP_RST_DEEPSLEEP: return "Deep-Sleep Wake";
      case ESP_RST_BROWNOUT:  return "Brownout";
      default:                return "Unknown";
    }
  }
#endif

  /// Emit one BOOT row to the fleet debug log. `rev` is the build's git revision
  /// (project-injected, e.g. the GIT_REV macro). `rssi` is read here so every device
  /// reports link quality uniformly. `vcc_mV` (0 = omit) is the supply/battery voltage;
  /// `vlow_mV` (0 = omit) is this build's low-battery WARN threshold -- pass
  /// avp::LowVcc_mV(kBattery) (Battery.hpp) so a monitor learns each device's threshold
  /// from its own boot line, with nothing to configure per device. `extra` carries only
  /// genuinely project-specific fields (e.g. "wake=60min"), or nullptr.
  inline void LogBoot(const char *version, const char *rev, const char *extra = nullptr,
                      uint16_t vcc_mV = 0, uint16_t vlow_mV = 0) {
#if defined(ESP8266)
    String      reason    = ESP.getResetReason();
    const char *reasonStr = reason.c_str();
#elif defined(ESP32)
    const char *reasonStr = ResetReasonStr(esp_reset_reason());
#else
#error "avp::LogBoot requires ESP8266 or ESP32"
#endif
    char tail[80];
    tail[0] = '\0';
    int n = 0;
    if(vcc_mV)  n += snprintf(tail + n, sizeof tail - n, ",vcc=%u", (unsigned)vcc_mV);
    if(vlow_mV) n += snprintf(tail + n, sizeof tail - n, ",vlow=%u", (unsigned)vlow_mV);
    if(extra && *extra) snprintf(tail + n, sizeof tail - n, ",%s", extra);
    debug_printf("\nBOOT,fw=%s,rev=%s,reason=%s,rssi=%ld%s\n",
                 version, rev, reasonStr, (long)WiFi.RSSI(), tail);
  } // LogBoot

  /// Emit one SLEEP row to the fleet debug log -- the lifecycle bookend to LogBoot,
  /// posted right before deep sleep. `sleep_s` is the deep-sleep length in seconds
  /// (0 = sleeps until an external wake, e.g. an event-driven device). `extra` is
  /// optional project-specific CSV column(s), or nullptr.
  inline void LogSleep(uint32_t sleep_s, const char *extra = nullptr) {
    const char *e   = (extra && *extra) ? extra : "";
    const char *sep = *e ? "," : "";
    if(sleep_s) debug_printf("\nSLEEP,for=%us%s%s\n", (unsigned)sleep_s, sep, e);
    else        debug_printf("\nSLEEP,for=until-wake%s%s\n", sep, e);
  } // LogSleep
} // namespace avp

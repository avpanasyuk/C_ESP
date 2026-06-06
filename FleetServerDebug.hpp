/**
 * @brief Standalone debug-to-fleet-server sink: mirror debug_puts() output to a
 *        shared CSV on the fleet server (http_server.py), tagged by device name.
 *
 * Each completed line is POSTed as
 *   "<logfile>,<name>,<line>"   ->  server writes:  <timestamp>,<name>,<line>
 * so one Debug_log.csv collects the whole fleet, with the device's netname in
 * the first data column.
 *
 * Wire it by teeing from a debug_puts() override:
 *   extern "C" int debug_puts(const char *s) {
 *     Serial.print(s);
 *     avp::FleetServerDebug::puts(s);   // <-- tee
 *     return 1;
 *   }
 * and configure once (after WiFi is up is fine; it is harmless earlier):
 *   avp::FleetServerDebug::begin("http://bsd:8000/", NAME);
 *
 * Designed to be safe to call from inside debug_puts():
 *   - line-buffers fragments, so one POST is sent per '\n' (one tidy row);
 *   - a re-entrancy guard stops the recursion that would otherwise happen
 *     because HTTP_POST_puts failures are themselves reported via debug_puts;
 *   - skips silently when WiFi is down or before begin();
 *   - commas inside a message land in extra columns (cosmetic).
 * Main-loop only: the POST blocks, so do NOT feed it from an ISR.
 *
 * Enabling it in a project also needs `+<C_ESP/client.cpp>` in build_src_filter
 * (provides HTTP_POST_puts).
 */
#pragma once
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

#include "C_ESP/service.h" // avp::HTTP_POST_puts

namespace avp {
  class FleetServerDebug {
    static constexpr size_t MaxLine = 160;
    static inline char        Line[MaxLine];
    static inline size_t      Len     = 0;
    static inline bool        Busy    = false;       // re-entrancy guard
    static inline const char *URL     = nullptr;     // e.g. "http://bsd:8000/"
    static inline const char *Name    = nullptr;     // device netname (NAME)
    static inline const char *LogFile = "Debug_log.csv";

   public:
    /// @param url     fleet server root, e.g. "http://bsd:8000/"
    /// @param name    device name placed in the first data column
    /// @param logfile CSV the lines are appended to (default "Debug_log.csv")
    static void begin(const char *url, const char *name, const char *logfile = "Debug_log.csv") {
      URL = url;
      Name = name;
      LogFile = logfile;
    }

    /// Feed debug_puts() output here. Buffers until '\n', then ships one row.
    static void puts(const char *s) {
      if(URL == nullptr || Busy) return; // not configured, or a re-entrant call
      for(; *s; ++s) {
        if(*s == '\r') continue;
        if(*s == '\n') ship();
        else if(Len < MaxLine - 1) Line[Len++] = *s;
        else ship(); // overlong line: flush what we have and keep going
      }
    }

   private:
    static void ship() {
      size_t n = Len;
      Len = 0;
      if(n == 0) return;
      Line[n] = '\0';
      if(WiFi.status() != WL_CONNECTED) return; // only post while connected
      Busy = true;
      char body[MaxLine + 64];
      snprintf(body, sizeof body, "%s,%s,%s", LogFile, Name ? Name : "?", Line);
      HTTP_POST_puts(URL, body); // result ignored on purpose -- never debug_puts here
      Busy = false;
    }
  }; // class FleetServerDebug
} // namespace avp

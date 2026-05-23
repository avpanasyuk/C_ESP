#pragma once
/**
 * @file RemoteLog.hpp
 * @brief CSV-framed remote logger; counterpart to csv_logging_server.py.
 *
 * Wire format on each POST: "<filename>, <payload>"
 * The server splits on the first comma, prepends a timestamp, and appends
 * the payload as one line to /tmp/logs/<filename>.log.
 *
 * Single static buffer (one instantiation per program); not thread-safe,
 * call only from the main loop.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "C_General/Error.hpp"
#include "client.hpp"

namespace avp {
  template<size_t MaxLineBytes = 2048>
  class RemoteLog {
    static constexpr const char *Sep = ", ";
    static inline const char *URL = nullptr;
    static inline char Buf[MaxLineBytes];

    static int writePrefix(const char *filename) {
      int n = snprintf(Buf, MaxLineBytes, "%s%s", filename, Sep);
      return (n < 0 || (size_t)n >= MaxLineBytes) ? -1 : n;
    }

    static void send(int total) {
      auto e = HTTP_POST_puts(URL, Buf, total);
      if(e != nullptr) debug_puts(e);
    }

  public:
    static void begin(const char *url) { URL = url; }

    /// Binary-safe: body may contain arbitrary bytes (e.g. ring-buffer dump).
    static void post(const char *filename, const char *body, size_t sz) {
      if(URL == nullptr) return;
      int n = writePrefix(filename);
      if(n < 0 || n + sz > MaxLineBytes) {
        debug_puts("RemoteLog: line too long\n");
        return;
      }
      memcpy(Buf + n, body, sz);
      send(n + sz);
    }

    static void postf(const char *filename, const char *fmt, ...)
      __attribute__((format(printf, 2, 3))) {
      if(URL == nullptr) return;
      int n = writePrefix(filename);
      if(n < 0) return;
      va_list ap;
      va_start(ap, fmt);
      int m = vsnprintf(Buf + n, MaxLineBytes - n, fmt, ap);
      va_end(ap);
      if(m < 0) return;
      if((size_t)m >= MaxLineBytes - n) m = MaxLineBytes - n - 1; // truncated
      send(n + m);
    }
  }; // class RemoteLog
} // namespace avp

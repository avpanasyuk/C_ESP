#pragma once
#include <stddef.h>

namespace avp {
  bool HTTP_POST_puts(const char *URL, const char *s, size_t sz);
  bool HTTP_POST_puts(const char *URL, const char *s);

  // Sink for HTTP_POST_puts's own failures; debug_puts would recurse (it backs the
  // FleetServerDebug tee). nullptr -> Serial fallback; StaticWebServer::begin sets
  // it to the /log buffer.
  inline void (*HTTP_POST_error_sink)(const char *) = nullptr;
} // namespace avp


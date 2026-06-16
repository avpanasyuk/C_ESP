#pragma once
#include <stddef.h>

namespace avp {
  bool HTTP_POST_puts(const char *URL, const char *s, size_t sz);
  bool HTTP_POST_puts(const char *URL, const char *s);
} // namespace avp


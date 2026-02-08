#pragma once
#include <stddef.h>

namespace avp {
  const char *HTTP_POST_puts(const char *URL, const char *s, size_t sz);
  const char *HTTP_POST_puts(const char *URL, const char *s);
} // namespace avp


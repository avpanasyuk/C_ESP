#pragma once

#include <stdint.h>
#include <WString.h>

namespace avp {
     // ***************** INTERNET CLIENT CONNECTION ************************************
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s = 0, const char *title = nullptr);
} // namespace avp



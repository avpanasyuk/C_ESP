#include "../C_General/General.hpp"
#include "service.h"

static String GET_responce;

Ticker SoftTimer;

namespace avp {
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s, const char *title) {
    static String out;
    if(!out) out.reserve(6000);
    out.clear();
    out += F("<!DOCTYPE html><html><head>");
    if(title != nullptr) {
      out += "<title>";
      out += title;
      out += "</title>";
    }
    if(AutoRefresh_s != 0) {
      out += F("<meta http-equiv=\"refresh\" content=\"");
      out += AutoRefresh_s;
      out += "\">";
    }
    out += "</head><body>";
    // out += avp::urlencode(html_body);
    out += html_body;
    out += "</body></html>";
#ifdef DEBUG
    // Serial.println(out);
#endif
    return out;
  } // GenerateHTML

  String String_vprintf(const char *format, va_list ap) {
    va_list ap_;
    va_copy(ap_, ap); // turns out vsnprintf is changing ap, so we have to make a reserve copy
    const int Size = vsnprintf(nullptr, 0, format, ap_);
    if(Size < 0) return "string_vprintf: format is wrong!";
    char Buffer[Size + 1]; // +1 to include ending zero byte
    vsnprintf(Buffer, Size + 1, format, ap);
    return String(Buffer); 
  } // string_vprintf

  PRINTF_WRAPPER(String, String_printf, String_vprintf);
} // namespace avp

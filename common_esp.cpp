#include <Arduino.h>
#include "C_ESP\General.h"

static String GET_responce;

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
  }  // GenerateHTML
} // namespace avp


#pragma once

/**
 * @brief class for logging messages into HTML code. It is a simple string
 * buffer with a break string in it.
 * "<br>" is inserted after each message. Old messages are deleted automatically
 * when the buffer is full.
 * @note there is a 0 at the end of filled string, so it can be used as a C string
 */
#include <cstring>
#include <C_General/Error.hpp>

namespace avp {
  class Log {
    static constexpr int DefaultSize = 2000;
    static inline char *Text{nullptr};
    static inline int Sz{0}; // max string length not counting trailing 0
    static inline const char *Br;
    static inline int BrL;

  public:
    static void begin(int size = DefaultSize, const char *Break = "<br>") {
      // should set all this stuff before assigning Text value
      Br = Break;
      BrL = strlen(Break);
      if(Sz != size) Text = (char *)realloc(Text, (Sz = size) + 1); // for trailing zero
      if(Text == nullptr) if(Serial) {
        Serial.println("Log failed to allocate a buffer");
        abort();
      }
      Text[0] = 0;
    } // begin

    static const char *Get() {
      if(Text == nullptr) begin();
      return Text;
    }

    static void IRAM_ATTR Add(const char *s, bool NoBreak = false) {
      if(Text == nullptr) begin();
      const int N{(int)strlen(s)};
      const int Length{(int)strlen(Text)};
      const int SpaceForBreak{NoBreak ? 0 : BrL};

      if(N + SpaceForBreak > Sz) Add("New entry is too big!");
      else {
        int Shift = Length + N + SpaceForBreak - Sz; // new string does not fit, how much I have to shift log up
        char *p = Text;

        if(Shift > 0) {                               // overran, got to shift at least by Shift 
          const char *pBr = strstr(Text + Shift, Br); // find next break after Shift

          if(pBr != nullptr) {
            pBr += BrL;                             // step over the last break, we do not need to copy it
            for(; *pBr != 0; ++p, ++pBr) *p = *pBr; // shift buffer
          }
        } else p += Length; // no shift, just step over the end of the string
        strcpy(p, s);
        if(!NoBreak) strcpy(p + N, Br);
      }
     } // Add
  }; // class Log
} // namespace avp
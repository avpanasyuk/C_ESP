#pragma once

/**
 * @brief class for logging messages into HTML code. It is a simple string
 * buffer with a break string in it.
 * "<br>" is inserted after each message. Old messages are deleted automatically
 * when the buffer is full.
 * @note there is a 0 at the end of filled string, so it can be used as a C string
 */
#include <cstddef>
#include <cstring>
#include <C_General/Error.hpp>

namespace avp {
  class Log {
    static constexpr size_t DefaultSize = 2000;
    static inline char *Text{nullptr};
    static inline size_t Sz; // max string length not counting trailing 0
    static inline const char *Br;
    static inline int BrL;

  public:
    static void begin(size_t size = DefaultSize, const char *Break = "<br>") {
     // should set all this stuff before assigning Text value
     Br = Break;
     BrL = strlen(Break);
     if(Text == nullptr) Text = (char *)malloc(Sz = size);
      else if(Sz != size)  Text = (char *)realloc(Text, Sz = size);
    } // begin

    static const char *Get() {
      if(Text == nullptr) begin();
      return Text;
    }

    static void Add(const char *s, bool NoBreak = false) {
      if(Text == nullptr) begin();
      size_t N = strlen(s);
      size_t Length = strlen(Text);
      int SpaceForBreak = NoBreak ? 0 : BrL;

      if(N + SpaceForBreak > Sz) Add("New entry is too big!");
      else {
        size_t Shift = Length + N + SpaceForBreak - Sz; // new string does not fit, how much I have to shift log up
        char *p = Text;

        if(Shift > 0) {                               // overran, got to shift
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
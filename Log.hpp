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
    static inline char *Text{nullptr};
    static inline int Sz; // max string length not counting trailing 0
    static inline const char *Br;
    static inline int BrL;

  public:
    static void begin(size_t size, const char *Break = "<br>") {
      AVP_ASSERT(Text == nullptr); // to prevent from being called twice
     // should set all this stuff before assigning Text value
      Sz = size;
      Br = Break;
      BrL = strlen(Break);

      PAUSE_INTERRUPTS
      (Text = new char[size + 1])[0] = 0;
    } // begin

    static const char *Get() {
      AVP_ASSERT(Text != nullptr);
      return Text;
    }

    static bool IsOpen() { return Text != nullptr; }

    static void Add(const char *s, bool NoBreak = false) {
      AVP_ASSERT(Text != nullptr);
      int N = strlen(s);
      int Length = strlen(Text);
      int SpaceForBreak = NoBreak ? 0 : BrL;

      if(N + SpaceForBreak > Sz) Add("New entry is too big!");
      else {
        int Shift = Length + N + SpaceForBreak - Sz; // new string does not fit, how much I have to shift log up
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
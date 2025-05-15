#pragma once

#include <Arduino.h>
#include <Ticker.h>
#include <WString.h>
#include <memory>
#include <stdint.h>
#include <string.h>

extern Ticker SoftTimer;

namespace avp {
  // ***************** INTERNET CLIENT CONNECTION
  // ************************************
  const String &GenerateHTML(const char *html_body, uint16_t AutoRefresh_s = 0, const char *title = nullptr);

  /**
   * @brief class for logging messages into HTML code. It is a simple string
   * buffer with a break string in it.
   * "<br>" is inserted after each message. Old messages are deleted automatically
   * when the buffer is full.
   * @note there is a 0 at the end of filled string, so it can be used as a C string
   */
  class Log {
    std::unique_ptr<char[]> Text;
    const int Sz; // max string length not counting trailing 0
    const char *const Br;
    const int BrL;

  public:
    Log(size_t size, const char *Break = "<br>")
        : Text{std::make_unique<char[]>(size + 1)}, Sz(size), Br(Break), BrL(strlen(Br)) {
      *Text.get() = 0;
    } // constructor

    const char *Get() const { return Text.get(); }

    void Add(const char *s, bool NoBreak = false) {
      int N = strlen(s);
      int Length = strlen(Text.get());
      int SpaceForBreak = NoBreak ? 0 : BrL;

      if (N + SpaceForBreak > Sz) Add("New entry is too big!");
      else {
        int Shift = Length + N + SpaceForBreak - Sz; // new string does not fit, how much I have to shift log up
        char *p = Text.get();

        if (Shift > 0) {                                    // overran, got to shift
          const char *pBr = strstr(Text.get() + Shift, Br); // find next break after Shift

          if (pBr != nullptr) {
            pBr += BrL; // step over the last break, we do not need to copy it
            for (; *pBr != 0; ++p, ++pBr) *p = *pBr; // shift buffer
          }
        } else p += Length; // no shift, just step over the end of the string
        strcpy(p, s);
        if (!NoBreak) strcpy(p + N, Br);
      }
    } // Add
  }; // class Log
} // namespace avp

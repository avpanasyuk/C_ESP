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
  class HTML_Log {
    static constexpr int DefaultSize = 2000;
    static constexpr int LastMsgBufSize = 256; // dedup buffer; longer messages skip dedup
    static inline char *Text{nullptr};
    static inline int Sz{0}; // max string length not counting trailing 0
    static inline const char *Br;
    static inline int BrL;
    static inline bool DoTimeMarks;
    // Dedup state: identical AddLine inputs in a row collapse to a single
    // entry followed by "#" markers, e.g. "Resolving bsd ...####<br>"
    // instead of the same message repeated N times.
    static inline char LastMsg[LastMsgBufSize];
    static inline int LastMsgLen{0};

  public:
    static void begin(bool DoTimeMarks_ = true, int size = DefaultSize, const char *Break = "<br>") {
      // should set all this stuff before assigning Text value
      Br = Break;
      BrL = strlen(Break);
      DoTimeMarks = DoTimeMarks_;
      if(Sz != size) Text = (char *)realloc(Text, (Sz = size) + 1); // for trailing zero
      if(Text == nullptr) abort();
      else Text[0] = 0;
    } // begin

    static const char *Get() {
      if(Text == nullptr) begin();
      return Text;
    }

    static void AddLine(const char *s, bool AddBreak = false) {
      AddLine(s, strlen(s), AddBreak);
    } // AddLine

    /**
     * @brief there is no newlines in s
     *
     * @param s
     * @param N
     * @param AddBreak
     */
    static void AddLine(const char *s, const int N, bool AddBreak = false) {
      if(Text == nullptr) begin();

      // Dedup: same content as last AddLine input -> append "#" to existing
      // tail instead of writing the full message again. Buffer ends up like
      // "msg<br>" -> "msg#<br>" -> "msg##<br>" -> ...
      if(LastMsgLen > 0 && N == LastMsgLen && memcmp(s, LastMsg, N) == 0) {
        int len = (int)strlen(Text);
        // Strip a trailing <br> if the buffer ends with one, so "#" appears
        // inline with the previous message rather than on a new line.
        if(len >= BrL && strncmp(Text + len - BrL, Br, BrL) == 0) {
          len -= BrL;
          Text[len] = 0;
        }
        if(len + 1 + (AddBreak ? BrL : 0) > Sz) return; // no room; skip dedup mark
        Text[len++] = '#';
        Text[len] = 0;
        if(AddBreak) strcpy(Text + len, Br);
        return;
      }

      // Different message -- remember it for the next dedup compare.
      if(N > 0 && N <= LastMsgBufSize) {
        memcpy(LastMsg, s, N);
        LastMsgLen = N;
      } else {
        LastMsgLen = 0;
      }

      const int Length{(int)strlen(Text)};
      const int SpaceForBreak{AddBreak ? BrL : 0};

      if(N + SpaceForBreak > Sz) AddLine("New entry is too big!", true);
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
        strncpy(p, s, N);
        if(AddBreak) strcpy(p + N, Br);
        else p[N] = 0; // strncpy does not always put trailing 0
      }
    } // AddLine

    /**
     * @brief function adds s to buffer Text, replacing '\n' with "<br>"
     * can be multiline
     * @param s
     * @param AddBreak - adds "<br>" at the end of s.
     */
    static void Add(const char *s, bool AddBreak = false) {
      Add(s, strlen(s), AddBreak);
    } // Add

    static void Add(const char *s, const int N, bool AddBreak = false) {
      if(Text == nullptr) begin(); // it should not happen

      // replace newlines with breaks, using recursion
      char *pos = (char *)memchr(s, '\n', N);
      if(pos != nullptr) { // found newline, sending first line and recurse the rest
        const int FirstLineN = pos - s;
        AddLine(s, FirstLineN, true);
        Add(pos + 1, N - FirstLineN - 1, AddBreak);
      } else AddLine(s, N, AddBreak);
    } // Add
  }; // class HTML_Log
} // namespace avp
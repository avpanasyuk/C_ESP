#pragma once

/**
 * @brief class for logging messages into HTML code. It is a simple string
 * buffer with a break string in it.
 * "<br>" is inserted after each message. Old messages are deleted automatically
 * when the buffer is full.
 * @note there is a 0 at the end of filled string, so it can be used as a C string
 */
#include <cstring>
#include "C_General/Error.hpp"
#include "C_General/General.hpp"

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
    // Optional timestamp prefix callback (set by project). Returns a static
    // C string prepended to each new entry when DoTimeMarks is true. Dedup
    // compares the message bytes only, so identical messages still collapse to
    // a single timestamped entry plus "#" markers.
    static inline const char *(*GetTimestamp)() = nullptr;

    /**
     * @brief (Re)allocate the log buffer and set formatting. Called lazily by the
     *        first Add/AddLine/Get, so explicit use is only needed to override
     *        the defaults. Aborts if the allocation fails.
     * @param DoTimeMarks_ prefix each new entry with GetTimestamp() (when set)
     * @param size buffer capacity in bytes, excluding the trailing '\0'
     * @param Break separator appended after entries (HTML line break)
     */
    static void begin(bool DoTimeMarks_ = true, int size = DefaultSize, const char *Break = "<br>") {
      // should set all this stuff before assigning Text value
      Br = Break;
      BrL = strlen(Break);
      DoTimeMarks = DoTimeMarks_;
      if(Sz != size) Text = (char *)realloc(Text, (Sz = size) + 1); // for trailing zero
      if(Text == nullptr) abort();
      else Text[0] = 0;
    } // begin

    /// @return the whole log as a NUL-terminated C string (allocates on first use).
    static const char *Get() {
      if(Text == nullptr) begin();
      return Text;
    }

    /// @brief Append one newline-free line, computing its length. @see AddLine(const char*,int,bool)
    static void AddLine(const char *s, bool AddBreak = false) {
      AddLine(s, strlen(s), AddBreak);
    } // AddLine

    /**
     * @brief Append one entry of @p N bytes, which must contain NO newline.
     *        Identical consecutive entries collapse to a single entry followed
     *        by '#' markers (dedup), and the oldest entries are dropped when the
     *        buffer would overflow.
     * @param s entry bytes (need not be NUL-terminated; @p N gives the length)
     * @param N number of bytes of @p s to append
     * @param AddBreak append the Break separator after the entry
     */
    static void AddLine(const char *s, const int N, bool AddBreak = false) {
      if(Text == nullptr) begin();

      // Dedup: same message bytes as the previous AddLine input -> append "#"
      // to existing tail instead of writing the full message again. Compares
      // only the message (not any timestamp prefix), so repeated entries still
      // collapse to "<ts> msg#" -> "<ts> msg##" -> ...
      if(LastMsgLen > 0 && N == LastMsgLen && memcmp(s, LastMsg, N) == 0) {
        int len = (int)strlen(Text);
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

      // Optional timestamp prefix; project supplies the format via GetTimestamp.
      const char *ts = (DoTimeMarks && GetTimestamp) ? GetTimestamp() : "";
      const int TsL = (int)strlen(ts);
      const int Length{(int)strlen(Text)};
      const int SpaceForBreak{AddBreak ? BrL : 0};
      const int EntryLen = TsL + N + SpaceForBreak;

      if(EntryLen > Sz) AddLine("New entry is too big!", true);
      else {
        int Shift = Length + EntryLen - Sz; // shift log up if buffer would overflow
        char *p = Text;

        if(Shift > 0) {
          const char *pBr = strstr(Text + Shift, Br); // find next break after Shift
          if(pBr != nullptr) {
            pBr += BrL;                             // skip past it
            for(; *pBr != 0; ++p, ++pBr) *p = *pBr; // shift buffer
          }
        } else p += Length; // no shift -- write at end

        if(TsL > 0) { memcpy(p, ts, TsL); p += TsL; }
        if(N > 0) { strncpy(p, s, N); p += N; }
        if(AddBreak) strcpy(p, Br);
        else *p = 0;
      }
    } // AddLine

    /**
     * @brief Append @p s, splitting on '\n' so each line becomes its own
     *        Break-terminated entry. Use this (not AddLine) for multi-line text.
     * @param s NUL-terminated text, may contain newlines
     * @param AddBreak append a trailing Break after the last line
     */
    static void Add(const char *s, bool AddBreak = false) {
      Add(s, strlen(s), AddBreak);
    } // Add

    /// @brief Length-delimited Add(): splits the first @p N bytes of @p s on '\n'.
    static void Add(const char *s, const int N, bool AddBreak = false) {
      if(Text == nullptr) begin();

      // Split on newlines: each becomes its own <br>-terminated line. If the
      // remainder after a newline is empty, don't recurse -- the previous
      // AddLine already wrote a trailing break; recursing would add a second
      // one and render as a visible blank line.
      char *pos = (char *)memchr(s, '\n', N);
      if(pos != nullptr) {
        const int FirstLineN = pos - s;
        const int RemainderN = N - FirstLineN - 1;
        AddLine(s, FirstLineN, true);
        if(RemainderN > 0) Add(pos + 1, RemainderN, AddBreak);
      } else AddLine(s, N, AddBreak);
    } // Add

    static void vprintf(const char *format, va_list ap) {
      svprintf_puts(+[](const char* s){ Add(s); }, format, ap);
    }
  }; // class HTML_Log
} // namespace avp
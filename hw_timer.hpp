#pragma once

#include <memory>
#include <atomic>
#ifdef ESP32
#include <soc/soc_caps.h>
#include <driver/timer.h>
#define NUM_HW_TIMERS SOC_TIMER_GROUP_TOTAL_TIMERS
// Prefer timer 2, to stay clear of 0 and 1 (RTOS and WiFi). The original ESP32/S2/S3 have
// four, but the C3/C2/H2 have only two, where a hard-coded 2 is out of range and trips the
// static_assert below. Falling back to the highest valid index keeps every existing target
// on exactly the timer it used before.
#define DEFAULT_TIMER (NUM_HW_TIMERS > 2 ? 2 : NUM_HW_TIMERS - 1)
#endif
#ifdef ESP8266
#define NUM_HW_TIMERS 1
#define TIMER_BASE_CLK (F_CPU / 16UL)
// #include <driver/hw_timer.h>
#define DEFAULT_TIMER 0
#endif

#include <Arduino.h>
#include "C_General/MyMath.hpp"
#include "C_General/Error.hpp"
#include "service.h"

namespace avp {
  void IRAM_ATTR onHW_Interrupt();
  /**
   * @brief this static class links to a single hardware timer interrupt with number Id
   * triggered each microsecond
   * You can create multiple SW timers to it with different period calling different function
   * up to a number MaxNumOfTimers
   * @tparam HWidx: index of hardware timer to use
   */
  template<uint8_t HWidx = DEFAULT_TIMER>
  class HW_Timer_ms {                                                      // counts milliseconds
    static_assert(HWidx < NUM_HW_TIMERS, "Wrong hardware timer index!");

    static constexpr uint8_t MaxNumOfTimers = 10; // I do not want to put it into template parameters,
    // as it would make it possible to generate several different HW_Timer_us with the same HWidx
  public:
    static inline class Timer_t {
      volatile int32_t CurrentTick; /* one tick takes one ms */
      /** @note timer and gpio interrupt have the same priority so do not interrupt each other
       * and I do not need mutexes. But as soon as there is a risk of interrupt triggered when
       * I am changing CurrentTick on foreground I will need one.
       */
      friend class HW_Timer_ms<HWidx>;
      bool (*fn)(); // if function returns true counter restarts

    public:
      uint32_t Period_ticks; ///< set to 0 to disable timer
      Timer_t(): Period_ticks(0) {}
      
      void IRAM_ATTR Start() { CurrentTick = Period_ticks - 1; }
      void IRAM_ATTR Stop() { CurrentTick = -1; }
    } Timer[MaxNumOfTimers];

    static void  FORCE_INLINE  _onHW_Interrupt() {
      for(uint_fast8_t TimerI = 0; TimerI < NumTimers; ++TimerI) {
        if(Timer[TimerI].CurrentTick < 0) continue;
        if(Timer[TimerI].CurrentTick == 0) {
          if(Timer[TimerI].fn != nullptr && Timer[TimerI].fn()) Timer[TimerI].Start();
        } else --Timer[TimerI].CurrentTick;
      }
    } // onHW_Interrupt

  private:
    static inline uint8_t NumTimers = 0; ///< number of created SW timers

     static void begin() {
      static bool Begun = false; ///< makes sure I do it once only
      if(!Begun) {
#ifdef ESP32
        uint16_t Divider = TIMER_BASE_CLK / 10000UL;  // hw clock 100 us, we need alarm_value in timerAlarmWrite
                                                      // to be higher than 1 with a margin, 10 seems ok
        hw_timer_t *ptimer = timerBegin(HWidx, Divider, true); //!!!!!!! NEVER EVER SET COUNT DOWN 
        timerAttachInterrupt(ptimer, onHW_Interrupt, true); // attaches interrupt handler, true = edge trigger, does not matter
        timerAlarmWrite(ptimer, 10 /* 1 ms */, true); // set count to trigger the interrupt and autoload
        yield(); // go to be here for some reason
        timerAlarmEnable(ptimer);
#endif
#ifdef ESP8266
        timer1_isr_init();
        timer1_attachInterrupt(onHW_Interrupt);

        //TIM_DIV256: 312.5Khz (1 tick = 3.2us - 26843542.4 us max)
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);

        // Interrupt every 500 ticks * 0.2us = 100us
        timer1_write(313 /* from 312.5 kHz to make 1 ms ticks */);

#endif
        Begun = true;
      }
    } // begin

  public:
    /// @brief  Attaches another timer counter to a hardware timer
    /// @param callback_fn -  should be declared IRAM_ATTR, if it returns true counter restarts
    /// @param Period_ms - if 0 timer is disabled
    /// @return reference Timer_t, Period_ticks and fn can be accessed, setting fn to nullptr
    ///         stops timer
    static Timer_t &CreateTimer(bool (*callback_fn)(), uint32_t Period_ms, bool DoStart = false) {
      begin();
      AVP_ASSERT(NumTimers < MaxNumOfTimers);
      Timer[NumTimers].Period_ticks = Period_ms;
      Timer[NumTimers].fn = callback_fn;
      if(DoStart) Timer[NumTimers].Start();
      else Timer[NumTimers].Stop();

      return Timer[NumTimers++];
    } // CreateTimer
  }; // HW_Timer

  // Free function (not a template static member — IRAM_ATTR doesn't apply there). inline so a header
  // included by more than one TU doesn't multiply-define it at link.
  inline void IRAM_ATTR onHW_Interrupt() { HW_Timer_ms<>::_onHW_Interrupt(); }
} // namespace avp

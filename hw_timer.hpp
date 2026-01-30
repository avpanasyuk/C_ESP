#pragma once

#include <memory>
#include <atomic>
#ifdef ESP32
#include <soc/soc_caps.h>
#include <driver/timer.h>
#define NUM_HW_TIMERS SOC_TIMER_GROUP_TOTAL_TIMERS
#endif
#ifdef ESP8266
#define NUM_HW_TIMERS 1
#include <driver/hw_timer.h>
#endif

#include <Arduino.h>
#include "C_General/MyMath.hpp"
#include "C_General/Error.hpp"
#include "service.h"

namespace avp {
  /**
   * @brief this static class links to a single hardware timer interrupt with number Id
   * triggered each microsecond
   * You can create multiple SW timers to it with different period calling different function
   * up to a number MaxNumOfTimers
   * @tparam HWidx: index of hardware timer to use
   */
  template<uint8_t HWidx = 2, uint16_t Divider = TIMER_BASE_CLK / 10000UL> // avoid timer 0 and 1. hw clock 100 us, we need alarm_value in timerAlarmWrite
                                                                           // to be higher than 1 with a margin, 10 seems ok
  class HW_Timer_ms {                                                      // counts milliseconds
    static_assert(HWidx != 0, "Used by FreeRTOS, do not mess!");
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

  private:
    static inline uint8_t NumTimers; ///< number of created SW timers

    static void IRAM_ATTR onHW_Interrupt() {
      for(uint_fast8_t TimerI = 0; TimerI < NumTimers; ++TimerI) {
        if(Timer[TimerI].CurrentTick < 0) continue;
        if(Timer[TimerI].CurrentTick == 0) {
          if(Timer[TimerI].fn != nullptr && Timer[TimerI].fn()) Timer[TimerI].Start();
        } else --Timer[TimerI].CurrentTick;
      }
    } // onHW_Interrupt

    static void begin() {
      static bool Begun = false; ///< makes sure I do it once only
      if(!Begun) {
        NumTimers = 0;
#ifdef ESP32
        hw_timer_t *ptimer = timerBegin(HWidx, Divider, true); //!!!!!!! NEVER EVER SET COUNT DOWN 
        timerAttachInterrupt(ptimer, onHW_Interrupt, true); // attaches interrupt handler, true = edge trigger, does not matter
        timerAlarmWrite(ptimer, 10 /* 1 ms */, true); // set count to trigger the interrupt and autoload
        yield(); // go to be here for some reason
        timerAlarmEnable(ptimer);
#endif
#ifdef ESP8266
        timer1_isr_init();
        timer1_attachInterrupt(onHW_Interrupt);

        // Enable timer with /16 prescaler, edge trigger, continuous mode
        // At 80MHz / 16 = 5MHz = 0.2us per tick
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP);

        // Interrupt every 500 ticks * 0.2us = 100us
        timer1_write(TIMER_BASE_CLK / 256 / 1000 /* to make 1 ms ticks */);

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

} // namespace avp

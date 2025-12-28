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

namespace avp {
  /**
   * @brief this static class links to a single hardware timer interrupt with number Id
   * triggered each microsecond
   * You can create multiple SW timers to it with different period calling different function
   * up to a number MaxNumOfTimers
   * @tparam HWidx: index of hardware timer to use
   */
  template<uint8_t HWidx = 0>
  class HW_Timer_ms { // counts milliseconds
    static_assert(HWidx < NUM_HW_TIMERS, "Wrong hardware timer index!");

    static constexpr uint8_t MaxNumOfTimers = 10; // I do not want to put it into template parameters,
// as it would make it possible to generate several different HW_Timer_us with the same HWidx
#ifdef ESP32
    static constexpr uint16_t Divider = TIMER_BASE_CLK / 1000000UL /* hw clock in us */;

    static inline hw_timer_t * const ptimer{timerBegin(HWidx, Divider, false)}; //  last false is for counting down, easier
#endif

    static inline class Timer_t {
      std::atomic<uint32_t> CurrentTick; /* one tick takes one ms */
      friend class HW_Timer_ms<HWidx>;

    public:
      uint32_t Period_ticks;
      void (*fn)();
    } Timer[MaxNumOfTimers];

    static inline uint8_t NumTimers = 0; ///< number of created SW timers
    static inline bool Beginned = false;

    static void IRAM_ATTR onHW_Interrupt() {
      for(uint8_t TimerI = 0; TimerI < NumTimers; ++TimerI) {
        if(!--Timer[TimerI].CurrentTick) {
          Timer[TimerI].CurrentTick = Timer[TimerI].Period_ticks;
          if(Timer[TimerI].fn != nullptr) Timer[TimerI].fn();
        }
      }
    } // onHW_Interrupt

  public:
    static void begin() {
      if(!Beginned) {
#ifdef ESP32

        timerAttachInterrupt(ptimer, onHW_Interrupt, true); // attaches interrupt handler, true = edge trigger, does not matter
        // Set alarm value: timer, value, autoreload
        timerAlarmWrite(ptimer, 1000 /* ms */, true); // set count to trigger the interrupt and autoload
        // Actually enable the alarm
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
        Beginned = true;
      }
    } // Begin
    /// @brief  Attaches another timer counter to a hardware timer
    /// @param callback_fn -  should be declared IRAM_ATTR
    /// @param Period_ms
    /// @return reference Timer_t, Period_ticks and fn can be accessed, setting fn to nullptr
    ///         stops timer
    static Timer_t &CreateTimer(void (*callback_fn)(), uint32_t Period_ms) {
      AVP_ASSERT(Beginned);
      AVP_ASSERT(Period_ms > 0);
      AVP_ASSERT(NumTimers < MaxNumOfTimers);
      Timer[NumTimers].Period_ticks = Period_ms;
      Timer[NumTimers].fn = callback_fn;

      return Timer[NumTimers++];
    } // CreateTimer
  }; // HW_Timer

} // namespace avp

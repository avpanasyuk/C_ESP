#pragma once

#include <Arduino.h>

namespace avp {
#ifdef ESP32
#define DIGITAL_WRITE_BODY                              \
  if(val) {                                             \
    if(pin < 32) {                                      \
      GPIO.out_w1ts = ((uint32_t)1 << pin);             \
    } else {                                            \
      GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32)); \
    }                                                   \
  } else {                                              \
    if(pin < 32) {                                      \
      GPIO.out_w1tc = ((uint32_t)1 << pin);             \
    } else {                                            \
      GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32)); \
    }                                                   \
  }

#define DIGITAL_READ_BODY                      \
  if(pin < 32) {                               \
    return (GPIO.in >> pin) & 0x1;             \
  } else {                                     \
    return (GPIO.in1.val >> (pin - 32)) & 0x1; \
  }

#define DIGITAL_TOGGLE_BODY                      \
  if(pin < 32) GPIO.out ^= ((uint32_t)1 << pin); \
  else GPIO.out1.val ^= ((uint32_t)1 << (pin - 32));
#endif
#ifdef ESP8266
#define DIGITAL_WRITE_BODY \
  if(val) {                \
    GPOS = (1 << pin);     \
  } else {                 \
    GPOC = (1 << pin);     \
  }
#define DIGITAL_READ_BODY return (GPI >> pin) & 0x1;
#define DIGITAL_TOGGLE_BODY               \
  if(GPI & (1 << pin)) GPOC = (1 << pin); \
  else GPOS = (1 << pin);
#endif

  template<uint8_t pin, bool val>
  inline void __attribute__((always_inline)) WritePin() { DIGITAL_WRITE_BODY }

  template<uint8_t pin>
  inline void __attribute__((always_inline)) WritePin(bool val) { DIGITAL_WRITE_BODY }
  inline void __attribute__((always_inline)) WritePin(uint8_t pin, bool val) { DIGITAL_WRITE_BODY }

  template<uint8_t pin>
  inline bool __attribute__((always_inline)) ReadPin() { DIGITAL_READ_BODY }
  inline bool __attribute__((always_inline)) ReadPin(uint8_t pin) { DIGITAL_READ_BODY }

  template<uint8_t pin>
  inline void __attribute__((always_inline)) TogglePin() { DIGITAL_TOGGLE_BODY }
  inline void __attribute__((always_inline)) TogglePin(int pin) { DIGITAL_TOGGLE_BODY }

  template<uint8_t pin>
  inline void __attribute__((always_inline)) SetPin() { WritePin<pin, 1>(); }
  inline void __attribute__((always_inline)) SetPin(uint8_t pin) { WritePin(pin, 1); }

  template<uint8_t pin>
  inline void __attribute__((always_inline)) ClearPin() { WritePin<pin, 0>(); }
  inline void __attribute__((always_inline)) ClearPin(uint8_t pin) { WritePin(pin, 0); }
} // namespace avp
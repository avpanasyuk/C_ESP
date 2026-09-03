#pragma once

#include <Arduino.h>

namespace avp {
#ifdef ESP32
#include <soc/soc_caps.h>
// The original ESP32/S2/S3 carry more than 32 GPIOs and a second bank of registers for the
// high ones (out1, in1, out1_w1ts, out1_w1tc). The C3/C2/H2 do not have those registers at
// all, so on those targets the high-bank branch cannot merely be unreachable -- it must not
// be compiled, or every one of these bodies fails to build even for a pin below 32.
#if SOC_GPIO_PIN_COUNT > 32
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
#else
// .val, unlike the >32-pin branch above: on these targets gpio_dev_t wraps each of these
// registers in an unnamed union, where the original ESP32's SDK declares them as bare
// uint32_t. Same register, different C type -- which is the other half of why this branch
// has to exist rather than the pin<32 path simply being shared.
#define DIGITAL_WRITE_BODY                     \
  if(val) {                                    \
    GPIO.out_w1ts.val = ((uint32_t)1 << pin);  \
  } else {                                     \
    GPIO.out_w1tc.val = ((uint32_t)1 << pin);  \
  }

#define DIGITAL_READ_BODY return (GPIO.in.val >> pin) & 0x1;

#define DIGITAL_TOGGLE_BODY GPIO.out.val ^= ((uint32_t)1 << pin);
#endif
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
  void FORCE_INLINE WritePin() { DIGITAL_WRITE_BODY }

  template<uint8_t pin>
  void FORCE_INLINE WritePin(bool val) { DIGITAL_WRITE_BODY }
  void FORCE_INLINE WritePin(uint8_t pin, bool val) { DIGITAL_WRITE_BODY }

  template<uint8_t pin>
  bool FORCE_INLINE ReadPin() { DIGITAL_READ_BODY }
  bool FORCE_INLINE ReadPin(uint8_t pin) { DIGITAL_READ_BODY }

  template<uint8_t pin>
  void FORCE_INLINE TogglePin() { DIGITAL_TOGGLE_BODY }
  void FORCE_INLINE TogglePin(uint8_t pin) { DIGITAL_TOGGLE_BODY }

  template<uint8_t pin>
  void FORCE_INLINE SetPin() { WritePin<pin, 1>(); }
  void FORCE_INLINE SetPin(uint8_t pin) { WritePin(pin, 1); }

  template<uint8_t pin>
  void FORCE_INLINE ClearPin() { WritePin<pin, 0>(); }
  void FORCE_INLINE ClearPin(uint8_t pin) { WritePin(pin, 0); }
} // namespace avp
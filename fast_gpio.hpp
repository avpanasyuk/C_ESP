#pragma once

#include <Arduino.h>

namespace avp {
#ifdef ESP32
  inline void __attribute__((always_inline)) digitalWrite(uint8_t pin, uint8_t val) {
    if(val) {
      if(pin < 32) {
        GPIO.out_w1ts = ((uint32_t)1 << pin);
      } else {
        GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));
      }
    } else {
      if(pin < 32) {
        GPIO.out_w1tc = ((uint32_t)1 << pin);
      } else {
        GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));
      }
    }
  }

  inline uint8_t __attribute__((always_inline)) digitalRead(uint8_t pin) {
    if(pin < 32) {
      return (GPIO.in >> pin) & 0x1;
    } else {
      return (GPIO.in1.val >> (pin - 32)) & 0x1;
    }
  }

  inline void __attribute__((always_inline)) digitalToggle(int pin) {
    if(pin < 32) GPIO.out ^= ((uint32_t)1 << pin);
    else GPIO.out1.val ^= ((uint32_t)1 << (pin - 32));
  }
#endif
#ifdef ESP8266
  inline void __attribute__((always_inline)) digitalWrite(uint8_t pin, uint8_t val) {
    if(val) {
      GPOS = (1 << pin); // Write 1 to Set register
    } else {
      GPOC = (1 << pin); // Write 1 to Clear register
    }
  }

  inline uint8_t __attribute__((always_inline)) digitalRead(uint8_t pin) {
    return (GPI >> pin) & 0x1; // Read Input register
  }

  inline void __attribute__((always_inline)) digitalToggle(int pin) {
    if(GPI & (1 << pin)) GPOC = (1 << pin);
    else GPOS = (1 << pin);
  }
#endif
} // namespace avp
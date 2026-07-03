/**
 * @file Battery.hpp
 * @brief Battery chemistry -> low-battery warn threshold, one place for the whole fleet.
 *
 * A device is battery-monitored by declaring its chemistry ONCE in the project (all
 * same-named devices share a battery type), e.g.
 *
 *     static constexpr avp::Battery kBattery = avp::Battery::LiFePO4;
 *
 * and passing avp::LowVcc_mV(kBattery) as LogBoot()'s vlow argument. The device then
 * self-reports its low-battery threshold in its BOOT line, so the bsd fleet_alarm learns
 * each device's threshold from its own log -- nothing to remember or configure per device.
 *
 * Thresholds are the point below which you want an alert while there is still useful
 * runtime left; tune here (the single source) if a chemistry's curve differs on your packs.
 * All values are ACTUAL battery mV -- a device with a resistive divider must report the
 * real voltage (scale the ADC reading in firmware), not the divided value.
 */
#pragma once
#include <stdint.h>

namespace avp {
  enum class Battery : uint8_t {
    None,         ///< mains-powered / no battery monitoring (LowVcc_mV -> 0 = never alarm)
    LiIonLDO,     ///< 1S LiIon behind a 3.3 V LDO; vcc = getVcc reads the regulated RAIL, which
                  ///< only sags once the pack nears LDO dropout (late, coarse -- all getVcc gives)
    LiIonDirect,  ///< 1S LiIon measured directly (ADC divider, reported as ACTUAL pack mV)
  };

  /// Low-battery WARN threshold in mV for a sensing topology (0 = not monitored).
  constexpr uint16_t LowVcc_mV(Battery b) {
    switch(b) {
      case Battery::LiIonLDO:    return 3200; // rail sagging => 1S LiIon near LDO dropout
      case Battery::LiIonDirect: return 3300; // actual pack at the knee of the discharge curve
      default:                   return 0;    // None
    }
  }
} // namespace avp

// Copyright 2015-2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*******************************************************************************
 * NOTICE
 * The hal is not public api, don't use in application code.
 * See readme.md in hal/include/hal/readme.md
 ******************************************************************************/

// The LL layer for ESP32 PCNT register operations

#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include "soc/pcnt_struct.h"
#include "hal/pcnt_types.h"

#ifndef AVP_RAM_ATTR
#define AVP_RAM_ATTR // set to IRAM_ATTR for ESP
#else
#if defined(ESP32) || defined(ESP8266)
#include <esp_attr.h>
#endif 
#endif


#ifdef __cplusplus
extern "C" {
#endif

#define PCNT_LL_GET_HW(num) (((num) == 0) ? (&PCNT) : NULL)
#define PCNT_LL_MAX_GLITCH_WIDTH 1023

// force 32-bit access to PCNT registers

#define FORCE_32_R(field)                                             \
  auto val32 = PCNT.field.val;                                        \
  using field_type = std::remove_reference_t<decltype((PCNT.field))>; \
  field_type *s = (field_type *)&val32;

#define FORCE_32_RMW(field, modify)                                   \
  FORCE_32_R(field)                                                   \
  {modify};                                                           \
  PCNT.field.val = val32; 

  typedef enum {
    PCNT_LL_EVENT_THRES1,
    PCNT_LL_EVENT_THRES0,
    PCNT_LL_EVENT_LOW_LIMIT,
    PCNT_LL_EVENT_HIGH_LIMIT,
    PCNT_LL_EVENT_ZERO_CROSS,
    PCNT_LL_EVENT_MAX
  } pcnt_ll_event_id_t;

#define PCNT_LL_EVENT_MASK ((1 << PCNT_LL_EVENT_MAX) - 1)


static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_clock() { FORCE_32_RMW(ctrl, s->clk_en = 1;); }
  /**
   * @brief Set PCNT channel edge action
   *
   *
   * @param unit PCNT unit number
   * @param channel PCNT channel number
   * @param pos_act Counter action when detecting positive edge
   * @param neg_act Counter action when detecting negative edge
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_edge_action(uint32_t unit, uint32_t channel,
    pcnt_channel_edge_action_t pos_act, pcnt_channel_edge_action_t neg_act) {
    FORCE_32_RMW(conf_unit[unit].conf0, if(channel == 0) {
      s->ch0_pos_mode = pos_act;
      s->ch0_neg_mode = neg_act; } else {
      s->ch1_pos_mode = pos_act;
      s->ch1_neg_mode = neg_act; })
  }

  /**
   * @brief Set PCNT channel level action
   *
   *
   * @param unit PCNT unit number
   * @param channel PCNT channel number
   * @param high_act Counter action when control signal is high level
   * @param low_act Counter action when control signal is low level
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_level_action(uint32_t unit, uint32_t channel, pcnt_channel_level_action_t high_act, pcnt_channel_level_action_t low_act) {
    FORCE_32_RMW(conf_unit[unit].conf0,
    if(channel == 0) {
      s->ch0_hctrl_mode = high_act;
      s->ch0_lctrl_mode = low_act;
    } else {
      s->ch1_hctrl_mode = high_act;
      s->ch1_lctrl_mode = low_act;
    });
  }

  static inline AVP_RAM_ATTR 
  void pcnt_ll_clear_interrupts() { PCNT.int_clr.val = ~(uint32_t)0; }

  /**
   * @brief Get pulse counter value
   *
   * @param unit  Pulse Counter unit number
   * @return PCNT count value (a signed integer)
   */
  static inline AVP_RAM_ATTR
  int16_t pcnt_ll_get_count(uint32_t unit) {
    FORCE_32_R(cnt_unit[unit]);
    return s->cnt_val;
  }

  /**
   * @brief Pause PCNT counter of PCNT unit
   *
   *
   * @param unit PCNT unit number
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_stop_count(uint32_t unit) {
    PCNT.ctrl.val |= 1 << (2 * unit + 1);
  }

  /**
   * @brief Resume counting for PCNT counter
   *
   *
   * @param unit PCNT unit number, select from uint32_t
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_start_count(uint32_t unit) {
    PCNT.ctrl.val &= ~(1 << (2 * unit + 1));
  }

  /**
   * @brief Clear PCNT counter value to zero
   *
   *
   * @param  unit PCNT unit number, select from uint32_t
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_clear_count(uint32_t unit) {
    PCNT.ctrl.val |= 1 << (2 * unit);
    PCNT.ctrl.val &= ~(1 << (2 * unit));
  }

  /**
   * @brief Enable PCNT interrupt for PCNT unit
   * @note  Each PCNT unit has five watch point events that share the same interrupt bit.
   *
   *
   * @param unit_mask PCNT units mask
   * @param enable True to enable interrupt, False to disable interrupt
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_intr(uint32_t unit_mask, bool enable) {
    if(enable) {
      PCNT.int_ena.val |= unit_mask;
    } else {
      PCNT.int_ena.val &= ~unit_mask;
    }
  }

  /**
   * @brief Get PCNT interrupt status
   *
   *
   * @return Interrupt status word
   */
  static inline AVP_RAM_ATTR
  uint32_t pcnt_ll_get_intr_status() {
    return PCNT.int_st.val;
  }

  /**
   * @brief Clear PCNT interrupt status
   *
   *
   * @param status value to clear interrupt status
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_clear_intr_status(uint32_t status) {
    PCNT.int_clr.val = status;
  }

  /**
   * @brief Enable PCNT high limit event
   *
   *
   * @param unit PCNT unit number
   * @param enable true to enable, false to disable
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_high_limit_event(uint32_t unit, bool enable) {
    FORCE_32_RMW(conf_unit[unit].conf0, s->thr_h_lim_en = enable;);
  }

  /**
   * @brief Enable PCNT low limit event
   *
   *
   * @param unit PCNT unit number
   * @param enable true to enable, false to disable
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_low_limit_event(uint32_t unit, bool enable) {
    FORCE_32_RMW(conf_unit[unit].conf0, s->thr_l_lim_en = enable;);
  }

  /**
   * @brief Enable PCNT zero cross event
   *
   *
   * @param unit PCNT unit number
   * @param enable true to enable, false to disable
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_zero_cross_event(uint32_t unit, bool enable) {
    FORCE_32_RMW(conf_unit[unit].conf0, s->thr_zero_en = enable;);
  }

  /**
   * @brief Enable PCNT threshold event
   *
   *
   * @param unit PCNT unit number
   * @param thres Threshold ID
   * @param enable true to enable, false to disable
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_thres_event(uint32_t unit, uint32_t thres, bool enable) {
    FORCE_32_RMW(conf_unit[unit].conf0,
    if(thres == 0) {
      s->thr_thres0_en = enable;
    } else {
      s->thr_thres1_en = enable;
    });
  }

  /**
   * @brief Disable all PCNT threshold events
   *
   *
   * @param unit unit number
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_disable_all_events(uint32_t unit) {
    PCNT.conf_unit[unit].conf0.val &= ~(PCNT_LL_EVENT_MASK << 11);
  }

  /**
   * @brief Set PCNT high limit value
   *
   *
   * @param unit PCNT unit number
   * @param value PCNT high limit value
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_high_limit_value(uint32_t unit, int value) {
    FORCE_32_RMW(conf_unit[unit].conf2, s->cnt_h_lim = value;);
  }

  /**
   * @brief Set PCNT low limit value
   *
   *
   * @param unit PCNT unit number
   * @param value PCNT low limit value
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_low_limit_value(uint32_t unit, int value) {
    FORCE_32_RMW(conf_unit[unit].conf2, s->cnt_l_lim = value;);
  }

  /**
   * @brief Set PCNT threshold value
   *
   *
   * @param unit PCNT unit number
   * @param thres Threshold ID
   * @param value PCNT threshold value
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_thres_value(uint32_t unit, uint32_t thres, int value) {
    FORCE_32_RMW(conf_unit[unit].conf1, 
      if(thres == 0) {
        s->cnt_thres0 = value;
    } else {
        s->cnt_thres1 = value;
    });
  }

  /**
   * @brief Get PCNT high limit value
   *
   *
   * @param unit PCNT unit number
   * @return PCNT high limit value
   */
  static inline AVP_RAM_ATTR 
  int16_t pcnt_ll_get_high_limit_value(uint32_t unit) {
    FORCE_32_R(conf_unit[unit].conf2);
    return s->cnt_h_lim;
  }

  /**
   * @brief Get PCNT low limit value
   *
   *
   * @param unit PCNT unit number
   * @return PCNT high limit value
   */
  static inline AVP_RAM_ATTR 
  int16_t pcnt_ll_get_low_limit_value(uint32_t unit) {
    FORCE_32_R(conf_unit[unit].conf2);
    return s->cnt_l_lim;
  }

  /**
   * @brief Get PCNT threshold value
   *
   *
   * @param unit PCNT unit number
   * @param thres Threshold ID
   * @return PCNT threshold value
   */
  static inline AVP_RAM_ATTR 
  int16_t pcnt_ll_get_thres_value(uint32_t unit, uint32_t thres) {
    FORCE_32_R(conf_unit[unit].conf1);
    return thres == 0 ? s->cnt_thres0 : s->cnt_thres1;
  }

  /**
   * @brief Get PCNT unit runtime status
   *
   *
   * @param unit PCNT unit number
   * @return PCNT unit runtime status
   */
  static inline AVP_RAM_ATTR 
  uint32_t pcnt_ll_get_unit_status(uint32_t unit) {
    return PCNT.status_unit[unit].val;
  }

  /**
   * @brief Get PCNT count sign
   *
   *
   * @param unit PCNT unit number
   * @return Count sign
   */
  static inline AVP_RAM_ATTR 
  pcnt_unit_count_sign_t pcnt_ll_get_count_sign(uint32_t unit) {
    return pcnt_unit_count_sign_t(PCNT.status_unit[unit].val & 0x03);
  }

  /**
   * @brief Get PCNT event status
   *
   *
   * @param unit PCNT unit number
   * @return Event status word
   */
  static inline AVP_RAM_ATTR 
  uint32_t pcnt_ll_get_event_status(uint32_t unit) {
    return PCNT.status_unit[unit].val >> 2;
  }

  /**
   * @brief Set PCNT glitch filter threshold
   *
   *
   * @param unit PCNT unit number
   * @param filter_val PCNT signal filter value, counter in APB_CLK cycles.
   *        Any pulses lasting shorter than this will be ignored when the filter is enabled.
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_set_glitch_filter_thres(uint32_t unit, uint32_t filter_val) {
    FORCE_32_RMW(conf_unit[unit].conf0, s->filter_thres = filter_val;);
  }

  /**
   * @brief Get PCNT glitch filter threshold
   *
   *
   * @param unit PCNT unit number
   * @return glitch filter threshold
   */
  static inline AVP_RAM_ATTR 
  uint32_t pcnt_ll_get_glitch_filter_thres(uint32_t unit) {
    FORCE_32_R(conf_unit[unit].conf0);
    return s->filter_thres;
  }

  /**
   * @brief Enable PCNT glitch filter
   *
   *
   * @param unit PCNT unit number
   * @param enable True to enable the filter, False to disable the filter
   */
  static inline AVP_RAM_ATTR 
  void pcnt_ll_enable_glitch_filter(uint32_t unit, bool enable) {
    FORCE_32_RMW(conf_unit[unit].conf0, s->filter_en = enable;);
  }

#ifdef __cplusplus
}
#endif

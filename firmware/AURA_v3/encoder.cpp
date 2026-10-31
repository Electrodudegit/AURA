// encoder.cpp - ESP32-specific: direct GPIO_IN1 register read inside the ISR.
#include "encoder.h"
#include "config.h"
#include <Arduino.h>
#include "soc/gpio_reg.h"
#include "esp_attr.h"
#include "esp_timer.h"

static_assert(PIN_ENC_CLK >= 32 && PIN_ENC_DT >= 32, "ISR reads GPIO_IN1_REG; CLK/DT must be GPIO32..39");

static volatile int32_t s_raw = 0;
static volatile uint32_t s_invalid = 0;
static volatile uint8_t s_prev = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int32_t s_consumed = 0;  // task side only
// esp_timer time (same base as millis()) of the first transition that COMPLETED a
// detent and has not been consumed yet. Partial-detent jiggle never starts the window.
static volatile int64_t s_first_us = 0;
static volatile bool s_first_valid = false;

// Gray-code transition table, index = (prev << 2) | cur, state = (CLK << 1) | DT.
// 0 = no move or invalid (double step); invalid ones are counted separately.
static const int8_t DRAM_ATTR k_qtab[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static inline uint8_t IRAM_ATTR read_state(void) {
  const uint32_t in1 = REG_READ(GPIO_IN1_REG);
  const uint8_t clk = (in1 >> (PIN_ENC_CLK - 32)) & 1u;
  const uint8_t dt = (in1 >> (PIN_ENC_DT - 32)) & 1u;
  return (uint8_t)((clk << 1) | dt);
}

static void IRAM_ATTR enc_isr(void) {
  const uint8_t cur = read_state();
  portENTER_CRITICAL_ISR(&s_mux);
  const uint8_t idx = (uint8_t)((s_prev << 2) | cur);
  const int8_t d = k_qtab[idx];
  if (d != 0) {
    s_raw = s_raw + d;
    // Detent boundary: s_consumed only moves in whole detents from 0, so a
    // completed detent is exactly s_raw landing on a multiple of the step count.
    // Constant power-of-two modulo -> inlined, no libgcc call from the ISR.
    if (!s_first_valid && (s_raw % ENC_STEPS_PER_DETENT) == 0) {
      s_first_us = esp_timer_get_time();  // IRAM-safe
      s_first_valid = true;
    }
  }
  else if (cur != s_prev && ((s_prev ^ cur) == 3)) s_invalid = s_invalid + 1;  // both bits flipped
  s_prev = cur;
  portEXIT_CRITICAL_ISR(&s_mux);
}

bool encoder_init(void) {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);  // KY-040 boards usually also have 10k pull-ups
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);   // switch has no pull-up on many KY-040 boards
  s_prev = read_state();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), enc_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT), enc_isr, CHANGE);
  return true;
}

int32_t encoder_raw(void) {
  portENTER_CRITICAL(&s_mux);
  int32_t r = s_raw;
  portEXIT_CRITICAL(&s_mux);
  return r;
}

uint32_t encoder_invalid_transitions(void) { return s_invalid; }

int32_t encoder_take_detents(void) {
  uint32_t t;
  return encoder_take_detents_ts(&t);
}

int32_t encoder_take_detents_ts(uint32_t *t_first_ms) {
  portENTER_CRITICAL(&s_mux);
  const int32_t raw = s_raw;
  const int64_t ft = s_first_us;
  portEXIT_CRITICAL(&s_mux);
  *t_first_ms = (uint32_t)(ft / 1000);
  // Wrap-safe difference.
  const int32_t diff = (int32_t)((uint32_t)raw - (uint32_t)s_consumed);
  const int32_t det = diff / ENC_STEPS_PER_DETENT;  // truncates toward zero
  s_consumed = (int32_t)((uint32_t)s_consumed + (uint32_t)(det * ENC_STEPS_PER_DETENT));
  // Close the latency window when a detent was consumed, or when the knob came
  // back to the already-consumed boundary (jiggle). Only if no new edge arrived
  // since we sampled s_raw, so a fresh detent's timestamp is never dropped.
  if (det != 0 || diff == 0) {
    portENTER_CRITICAL(&s_mux);
    if (s_raw == raw) s_first_valid = false;
    portEXIT_CRITICAL(&s_mux);
  }
  return det;
}

// ---- switch, task context ----
static bool s_sw_stable_down = false;
static bool s_sw_last_raw = false;
static uint32_t s_sw_change_ms = 0;
static uint32_t s_sw_down_ms = 0;
static bool s_sw_long_fired = false;

bool encoder_switch_down(void) { return s_sw_stable_down; }

EncEvent encoder_poll_switch(uint32_t now) {
  const bool raw_down = digitalRead(PIN_ENC_SW) == LOW;
  if (raw_down != s_sw_last_raw) {
    s_sw_last_raw = raw_down;
    s_sw_change_ms = now;
  }
  EncEvent ev = ENC_EV_NONE;
  if (raw_down != s_sw_stable_down && (uint32_t)(now - s_sw_change_ms) >= ENC_SW_DEBOUNCE_MS) {
    s_sw_stable_down = raw_down;
    if (raw_down) {
      s_sw_down_ms = now;
      s_sw_long_fired = false;
    } else if (!s_sw_long_fired) {
      ev = ENC_EV_CLICK;
    }
  }
  if (s_sw_stable_down && !s_sw_long_fired && (uint32_t)(now - s_sw_down_ms) >= ENC_SW_LONG_MS) {
    s_sw_long_fired = true;
    ev = ENC_EV_LONG;
  }
  return ev;
}

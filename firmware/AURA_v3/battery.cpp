// battery.cpp - Arduino-ESP32 analogReadMilliVolts() (uses eFuse calibration).
// ASSUMPTION: divider ratio BATT_DIVIDER (default 2.0) is unverified.
#include "battery.h"
#include "config.h"
#include "data_store.h"
#include <Arduino.h>

static volatile int8_t s_last_pct = -1;
int8_t battery_last_pct(void) { return s_last_pct; }
struct CurvePt { uint16_t mv; uint8_t pct; };
// Approximate single-cell Li-ion resting curve (light load). Not a fuel gauge.
static const CurvePt k_curve[] = {
  {3300, 0}, {3500, 5}, {3600, 12}, {3650, 20}, {3700, 30}, {3750, 40},
  {3800, 50}, {3900, 65}, {4000, 80}, {4100, 90}, {4200, 100},
};
static uint32_t s_next_ms = 0;

bool battery_init(void) {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);  // ~0.15..2.45 V usable at the pin
  return true;
}

int8_t battery_pct_from_mv(uint16_t mv) {
  const size_t n = sizeof(k_curve) / sizeof(k_curve[0]);
  if (mv < 2500 || mv > 4500) return -1;  // implausible: no battery / wrong divider
  if (mv <= k_curve[0].mv) return 0;
  if (mv >= k_curve[n - 1].mv) return 100;
  for (size_t i = 1; i < n; i++) {
    if (mv <= k_curve[i].mv) {
      const int32_t span_mv = k_curve[i].mv - k_curve[i - 1].mv;
      const int32_t span_p = k_curve[i].pct - k_curve[i - 1].pct;
      return (int8_t)(k_curve[i - 1].pct + ((int32_t)(mv - k_curve[i - 1].mv) * span_p) / span_mv);
    }
  }
  return 100;
}

void battery_poll(uint32_t now) {
  if ((int32_t)(now - s_next_ms) < 0) return;
  s_next_ms = now + BATT_PERIOD_MS;
  uint32_t sum = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) sum += analogReadMilliVolts(PIN_BATT_ADC);
  BattData b;
  b.adc_mv = (uint16_t)(sum / N);
  float v = (float)b.adc_mv * BATT_DIVIDER;
  if (v > 65535.0f) v = 65535.0f;
  b.vbat_mv = (uint16_t)v;
  b.pct = battery_pct_from_mv(b.vbat_mv);
  s_last_pct = b.pct;
  ds_set_batt(&b);
}

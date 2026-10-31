// battery.h - Li-ion battery voltage/percent on GPIO34 (ADC1).
#pragma once
#include <stdint.h>
#include <stdbool.h>
bool battery_init(void);
// Call periodically from one task; samples every BATT_PERIOD_MS and publishes to data_store.
void battery_poll(uint32_t now_ms);
int8_t battery_pct_from_mv(uint16_t vbat_mv);
int8_t battery_last_pct(void);  // v3.1.3: last published percent (-1 = none yet)

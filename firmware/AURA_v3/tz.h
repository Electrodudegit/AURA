// tz.h - device timezone table. Host-safe. The active index lives here so the UI
// task and the net task share it without reading each other's structs.
#pragma once
#include <stdint.h>

uint8_t tz_count(void);
uint8_t tz_get(void);
void tz_select(uint8_t i);
uint32_t tz_gen(void);             // bumps on every select
const char *tz_label(uint8_t i);  // short name drawn in Settings
const char *tz_posix(uint8_t i);  // POSIX TZ string for configTzTime
const char *tz_om(uint8_t i);     // Open-Meteo timezone query value (already encoded)
int tz_std_min(uint8_t i);        // standard offset, minutes east of UTC (no DST)

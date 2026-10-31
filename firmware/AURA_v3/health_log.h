// health_log.h - periodic heap / stack / fps report on serial.
#pragma once
#include <stdint.h>
// Registers the heap failed-alloc hook. Call first thing in setup().
bool health_init(void);
void health_poll(uint32_t now_ms);   // call often; prints every HEALTH_PERIOD_MS
void health_print_now(void);
void health_heap_dump(void);       // v3.1.3 `heap` bench command (free, largest, min-ever, per-cap, gates, stacks)
struct AuraData;
AuraData *health_scratch(void);  // shared snapshot buffer (ble task: CLI + health only)

// touch_xpt2046.h - raw XPT2046 driver: sampling, filtering, debounce.
// Runs in ui_task (sole SPI owner). The display bus must be idle before touch_tick().
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "touch_cal.h"

struct TouchSample {
  bool down;          // debounced state
  int16_t x, y;       // filtered screen coords (valid while down; last value after release)
  uint16_t rx, ry;    // trimmed-mean raw of the last valid sample
  uint16_t z;         // pressure of the last read (z1 + 4095 - z2)
  uint32_t t_ms;      // time of last valid sample
  bool fresh;         // a valid sample was taken on this tick
};

struct TouchStats {
  uint32_t reads, skipped, spi_err, rej_z, rej_range, rej_spread, rej_jump, irq_edges, fallback_reads;
};

enum TouchIrqStatus : uint8_t { TOUCH_IRQ_UNKNOWN = 0, TOUCH_IRQ_SEEN, TOUCH_IRQ_UNROUTED };

// Adds the touch device (2 MHz) to the already-initialised display bus,
// configures PENIRQ (falling-edge ISR = hint only) and loads calibration.
bool touch_init(void);
// true while PENIRQ is low (pen down hint).
bool touch_irq_low(void);
// One 100 Hz tick. When idle and PENIRQ is high with no edge, the SPI read is
// skipped except for a ~20 Hz fallback poll (TOUCH_FALLBACK_MS), so touch still
// works if GPIO22 is not routed. Once pressed, every tick reads (100 Hz).
// Fills *out with the current state. Returns false only on an SPI error
// (state left unchanged, *out = previous state).
bool touch_tick(uint32_t now_ms, TouchSample *out);
TouchIrqStatus touch_irq_status(void);
void touch_set_cal(const TouchCal *c, bool from_nvs);
const TouchCal *touch_get_cal(void);
bool touch_cal_from_nvs(void);
void touch_set_jump_reject(bool on);
void touch_get_stats(TouchStats *s);

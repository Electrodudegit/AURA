// prof.h - cheap compose profiler: exclusive time per layer, per frame.
// prof_push(L)/prof_pop() nest; time is charged to the innermost layer only.
// Device: esp_timer_get_time() (~1 us per call pair). Host: steady_clock.
// ui_task prints one [prof] line per second (averages per frame).
#pragma once
#include <stdint.h>
enum ProfLayer : uint8_t {
  PL_OTHER = 0,   // screen code not in a named layer (layout, widgets, shapes)
  PL_BG,          // fills, gradients
  PL_AURORA,      // aurora ribbons / stars
  PL_TILES,       // split-flap tiles, cards, glass
  PL_TEXT,        // all text (1-bit and AA)
  PL_PART,        // particles, rain/snow
  PL_FACE,        // face art: gauges, ring, hands, buddy, icons
  PL_COUNT
};
extern const char *const k_prof_names[PL_COUNT];
uint32_t prof_now_us(void);
void prof_push(uint8_t layer);
void prof_pop(void);
void prof_frame_reset(void);                    // start of compose
void prof_get(uint32_t out_us[PL_COUNT]);       // accumulated since prof_frame_reset
struct ProfScope {
  explicit ProfScope(uint8_t l) { prof_push(l); }
  ~ProfScope() { prof_pop(); }
};
#define PROF_SCOPE(l) ProfScope prof_scope_##__LINE__(l)

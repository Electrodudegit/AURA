// apps.h - internal: per-app entry points (apps_*.cpp) and shared helpers.
#pragma once
#include "ui_core.h"

#define APP_FN(n)                                  \
  void n##_enter(uint32_t now);                    \
  void n##_leave(void);                            \
  void n##_input(const UiIn &in, uint32_t now);    \
  void n##_draw(void);
APP_FN(weather) APP_FN(forecast) APP_FN(sky) APP_FN(air) APP_FN(crypto) APP_FN(stocks) APP_FN(news)
APP_FN(cricket) APP_FN(focus) APP_FN(world) APP_FN(system) APP_FN(oracle) APP_FN(buddyapp)
APP_FN(macro) APP_FN(pc)
#undef APP_FN
void pc_tick(uint32_t now);
void pc_debug_page(uint8_t page, uint8_t row);
bool pc_token_visible(void);
bool oracle_idle_locked(void);
void oracle_debug_focus(uint8_t focus);
void oracle_debug_draft(const char *text);
void oracle_debug_keypad(uint8_t mode, uint8_t focus);
void macro_debug_flash(uint8_t layer, uint8_t key);
// v3.1.4: SCR_APP per-frame tick + band keys (only the Buddy app has a key; others draw every band)
void buddyapp_tick(uint32_t now);
uint32_t buddyapp_band_key(int band, BandWin *w);
void oracle_tick(uint32_t now);  // drives the Buddy rig (think / speak / outcome)

// focus timer state is shared with the menu tile value
struct FocusState { uint8_t st; uint32_t t_end, t_left; uint8_t sessions; };  // st 0 idle 1 run 2 pause 3 break
extern FocusState g_focus;
// mark the current input as handled (suppresses the global back gestures)
void ui_consume(void);
// procedural icons for Macro / Settings (no mask in the asset set)
// World clock helper: UTC offset (minutes) for a city with DST rules at time t (UTC epoch)
int world_offset_min(uint8_t city, time_t t);
// Home visuals shared with the saver (home_screen.cpp)
// One split-flap tile showing ch; during a flip (0 <= u < 1) the old char prev folds away.
void home_flap(int x, int y, char ch, char prev, float u, bool glow);
uint32_t home_release_caches(void);  // v3.1.3: drop HOME heap caches (bytes freed)
uint32_t home_cache_bytes(void);
void home_face_step(int dir);        // bench: next / prev face
void home_face_set(uint8_t idx);     // bench: face index 0..3 (F1, F2, F5, F6)
// Settings entry points used by apps
void settings_open_wifi(void);    // Settings > Phone setup (portal)
void settings_open_creds(bool first_boot);  // Settings > Credentials, before Home on first boot
void settings_open_pckeys(void);  // jump to Settings with "PC keys (Bluetooth)" focused
void focus_tick(uint32_t now);   // pomodoro timer, runs every frame
void cal_on_raw(const UiTouchRaw &r, uint32_t now);  // calibration sampler (touch tick rate)

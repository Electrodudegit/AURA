// ui_task.h - core 1 UI task: sole owner of the SPI bus (ILI9341 + XPT2046).
#pragma once
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool ui_task_start(void);
TaskHandle_t ui_task_handle(void);
// Thread-safe requests (set a flag consumed by ui_task on its next frame).
void ui_request_cal(void);    // Settings > Touch calibration (CLI shortcut)
void ui_request_grid(void);   // Settings > Diagnostics > tap grid (CLI shortcut)
void ui_request_snap(void);   // serial RGB565 dump of the screen
// Worst frame time (ms) since the previous call; resets to 0 (called by health_log).
uint32_t ui_take_worst_ms(void);

// ---- v3.1.3 -----------------------------------------------------------------
// UI heap caches vs TLS: net_task calls ui_cache_release() before a TLS gate (the UI frees
// its caches at the next frame start and holds new builds off) and ui_cache_resume() when
// the session is over. Returns the bytes freed, or UINT32_MAX if the UI did not ack in time.
uint32_t ui_cache_release(uint32_t timeout_ms);
// Same handshake. lend_bands=false drops HOME caches only and keeps the double-buffered
// band pair. net_task tries that first and lends the pair only if the gate still fails.
uint32_t ui_cache_release_ex(uint32_t timeout_ms, bool lend_bands);
void ui_cache_resume(void);
bool ui_cache_held(void);
uint32_t ui_releasable_bytes(void);  // what ui_cache_release() would free now (HOME caches + band slot 1)
void ui_force_full_frame(void);
// Serial bench commands (setup_cli): queued, executed by ui_task at its next frame start.
enum UiBenchOp : uint8_t { UB_FACE_STEP = 1, UB_FACE_SET, UB_MENU_OPEN, UB_MENU_CLOSE, UB_APP, UB_BACK, UB_HOME };
bool ui_bench(uint8_t op, int16_t arg);
void ui_prof_enable(bool on);   // [prof] line once per second (default on)
bool ui_prof_enabled(void);

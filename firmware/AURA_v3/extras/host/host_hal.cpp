// host_hal.cpp - tiny HAL stub for the capture shim (no hardware, no network).
#include "ui_core.h"
#include "tz.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

bool g_host_verbose = false;
void hal_log(const char *fmt, ...) {
  if (!g_host_verbose) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
uint32_t hal_heap_free(void) { return 112640; }
uint32_t hal_heap_largest(void) { return 69632; }
uint32_t hal_heap_min(void) { return 84220; }
bool g_host_nocache = false;  // --prof-bands: model the hardware (caches refused)
bool hal_cache_ok(uint32_t) { return !g_host_nocache; }
void hal_lat_tag(const char *) {}
uint32_t hal_uptime_s(void) { return 3 * 3600 + 25 * 60 + 12; }
void hal_backlight(uint8_t) {}
static bool s_ble_en = true;
void hal_ble_enable(bool on) { s_ble_en = on; }
void hal_ble_kick(void) {}
void hal_hid_consumer(uint16_t, uint8_t) {}
void hal_hid_key(uint8_t, uint8_t) {}
void hal_tz_apply(uint8_t i) { tz_select(i); }
void hal_comp_enable(bool) {}
void hal_comp_forget(void) {}
void hal_portal(bool) {}
void hal_net_refresh(int) {}
bool hal_nvs_has_ssid(void) { return false; }
void hal_cred_query(HalCredQuery *q) { memset(q, 0, sizeof(*q)); }
int hal_cred_commit(const HalCredCommit *) { return 0; }
static uint32_t s_or_seq = 0;
uint32_t hal_oracle_ask(const char *prompt) { hal_log("[host] oracle ask (%u chars)", (unsigned)strlen(prompt)); return ++s_or_seq; }
bool hal_prefs_load(UiPrefs *) { return false; }
void hal_prefs_save(const UiPrefs *) {}
bool hal_cal_solve_save(const int32_t[4][2], const int16_t[4][2], int32_t *err) { *err = 2; return true; }
void hal_cal_restore_jump_reject(bool) {}
const char *hal_cal_source(void) { return "default"; }
void hal_snap(void) {}
void hal_diag(HalDiag *d) {
  memset(d, 0, sizeof(*d));
  d->fps_x10 = 250; d->worst_ms = 31; d->lat_max = 46; d->t_reads = 18234; d->t_rej_z = 812; d->t_rej_jump = 3;
  d->irq = "high"; d->enc_raw = 128;
}
void hal_reboot(void) {}

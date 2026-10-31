// ui_hal.h - services the UI needs from the platform. Implemented by
// ui_task.cpp (device) and host/host_main.cpp (capture shim).
#pragma once
#include <stdint.h>
#include <stdbool.h>
struct UiPrefs;

void hal_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t hal_heap_free(void);
uint32_t hal_heap_largest(void);
uint32_t hal_heap_min(void);
uint32_t hal_uptime_s(void);
void hal_backlight(uint8_t pct);           // 0..100
// BLE 'PC keys': enable gate (Settings, NVS) + kick = start advertising for a
// 2-min no-connect window (boot, Macro app open, Settings press).
void hal_ble_enable(bool on);
void hal_ble_kick(void);
void hal_hid_consumer(uint16_t usage, uint8_t n);
void hal_hid_key(uint8_t modifiers, uint8_t keycode);  // HID keyboard report (shortcuts). Nothing is logged.
void hal_tz_apply(uint8_t idx);        // select timezone and reconfigure the clock
void hal_comp_enable(bool on);         // local companion listener (no cloud)
void hal_comp_forget(void);            // rotate the pairing token and drop the bound laptop
void hal_portal(bool on);                  // Wi-Fi setup captive portal (secondary)
void hal_net_refresh(int job);             // -1 = all
// On-device credentials. NVS wins over secrets.h. Lengths only.
// key_len order: stocks, cricket, oracle (xAI), groq, owm, gemini.
struct HalCredQuery {
  char ssid[33];
  uint8_t pass_len;
  uint8_t key_len[6];
};
struct HalCredCommit {
  const char *ssid;
  const char *pass;
  bool write_wifi;
  uint8_t key_op[6];  // 0 keep, 1 write key[i], 2 clear
  const char *key[6];
};
bool hal_nvs_has_ssid(void);
void hal_cred_query(HalCredQuery *q);
int hal_cred_commit(const HalCredCommit *c);  // 0 saved, 1 Wi-Fi rejected, 2 NVS error
bool hal_prefs_load(UiPrefs *p);
void hal_prefs_save(const UiPrefs *p);
bool hal_cal_solve_save(const int32_t raw[4][2], const int16_t pts[4][2], int32_t *err_px);
void hal_cal_restore_jump_reject(bool on);
const char *hal_cal_source(void);          // "nvs" / "default"
uint32_t hal_oracle_ask(const char *prompt);  // queue an Oracle question (net_task), returns its seq
void hal_snap(void);                       // dump the next frame over serial
// v3.1.3 UI heap caches (hero masks): true if a cache of n bytes may be built now. False while
// a TLS job holds the release (net_task) or if it would eat into the TLS reserve.
bool hal_cache_ok(uint32_t n);
// v3.1.4: tag the pending input for the next [lat] line (" pop=<tag>", never rate-limited).
void hal_lat_tag(const char *tag);
struct HalDiag {
  uint16_t fps_x10; uint32_t worst_ms, overruns, band_fail, touch_bus, touch_spi, lat_max;
  uint32_t t_reads, t_rej_z, t_rej_range, t_rej_spread, t_rej_jump, t_fallback;
  const char *irq; int32_t enc_raw; uint32_t enc_inv;
};
void hal_diag(HalDiag *d);
void hal_reboot(void);

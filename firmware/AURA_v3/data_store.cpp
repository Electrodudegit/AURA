#include "data_store.h"
#include <string.h>
#ifndef AURA_HOST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static StaticSemaphore_t s_mtx_buf;
static SemaphoreHandle_t s_mtx = nullptr;
static bool lock(uint32_t ms) { return s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(ms)) == pdTRUE; }
static void unlock(void) { xSemaphoreGive(s_mtx); }
#else
static bool lock(uint32_t) { return true; }  // host shim: single thread
static void unlock(void) {}
#endif
static AuraData s_data;
static uint8_t s_art[COMP_ART_MAX];  // v3.1.6 Now Playing art, RGB565 LE

bool ds_init(void) {
  memset(&s_data, 0, sizeof(s_data));
  s_data.batt.pct = -1;
  s_data.net.tls_largest_min = UINT32_MAX;
  s_data.net.tls_heap_min = UINT32_MAX;
  s_data.wx.sunrise_min = s_data.wx.sunset_min = -1;
#ifndef AURA_HOST
  s_mtx = xSemaphoreCreateMutexStatic(&s_mtx_buf);
  return s_mtx != nullptr;
#else
  return true;
#endif
}

bool ds_get(AuraData *out, uint32_t ms) {
  if (!lock(ms)) return false;
  *out = s_data;
  unlock();
  return true;
}
bool ds_set_net(const NetData *n, uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.net = *n;
  unlock();
  return true;
}
bool ds_get_net(NetData *n, uint32_t ms) {
  if (!lock(ms)) return false;
  *n = s_data.net;
  unlock();
  return true;
}
bool ds_set_ble(const BleData *b, uint32_t ms) {
  if (!lock(ms)) return false;
  bool hm = s_data.ble.hid_mode;  // hid_mode is owned by the UI
  s_data.ble = *b;
  s_data.ble.hid_mode = hm;
  unlock();
  return true;
}
bool ds_set_hid_mode(bool on, uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.ble.hid_mode = on;
  unlock();
  return true;
}
bool ds_set_batt(const BattData *b, uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.batt = *b;
  unlock();
  return true;
}
bool ds_set_ui(const UiData *u, uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.ui = *u;
  unlock();
  return true;
}

#define DS_SETTER(name, type, field)                 \
  bool name(const type *v, uint32_t ms) {            \
    if (!lock(ms)) return false;                     \
    s_data.field = *v;                               \
    unlock();                                        \
    return true;                                     \
  }
DS_SETTER(ds_set_wx, WxData, wx)
DS_SETTER(ds_set_air, AirData, air)
DS_SETTER(ds_set_crypto, CryptoData, crypto)
DS_SETTER(ds_set_news, NewsData, news)
DS_SETTER(ds_set_stocks, StocksData, stocks)
DS_SETTER(ds_set_cricket, CricketData, cricket)
DS_SETTER(ds_set_keys, KeysData, keys)
DS_SETTER(ds_set_loc, LocData, loc)
DS_SETTER(ds_set_portal, PortalData, portal)
DS_SETTER(ds_set_oracle, OracleData, oracle)
DS_SETTER(ds_set_comp, CompData, comp)
DS_SETTER(ds_set_pc, PcStatusData, pc)
bool ds_set_job(uint8_t job, const JobInfo *j, uint32_t ms) {
  if (job >= JOB_COUNT || !lock(ms)) return false;
  s_data.job[job] = *j;
  unlock();
  return true;
}
bool ds_set_time_valid(bool v, uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.time_valid = v;
  unlock();
  return true;
}

bool ds_set_comp_now(const CompNowData *c, uint32_t ms) {
  if (!c || !lock(ms)) return false;
  const uint8_t w = s_data.comp_now.art_w, h = s_data.comp_now.art_h;
  const uint16_t seq = s_data.comp_now.art_seq;
  s_data.comp_now = *c;
  s_data.comp_now.art_w = w;
  s_data.comp_now.art_h = h;
  s_data.comp_now.art_seq = seq;
  unlock();
  return true;
}
bool ds_comp_art_begin(uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.comp_now.art_w = s_data.comp_now.art_h = 0;
  unlock();
  return true;
}
bool ds_comp_art_write(uint16_t off, const uint8_t *src, uint16_t n, uint32_t ms) {
  if (!src || (uint32_t)off + n > sizeof(s_art)) return false;
  if (!lock(ms)) return false;
  memcpy(s_art + off, src, n);
  unlock();
  return true;
}
bool ds_comp_art_commit(uint8_t w, uint8_t h, uint32_t ms) {
  if (w == 0 || h == 0 || w > COMP_ART_MAX_DIM || h > COMP_ART_MAX_DIM || (uint32_t)w * h * 2 > sizeof(s_art)) return false;
  if (!lock(ms)) return false;
  s_data.comp_now.art_w = w;
  s_data.comp_now.art_h = h;
  s_data.comp_now.art_seq++;
  unlock();
  return true;
}
bool ds_comp_art_clear(uint32_t ms) {
  if (!lock(ms)) return false;
  s_data.comp_now.art_w = s_data.comp_now.art_h = 0;
  s_data.comp_now.art_seq++;
  memset(s_art, 0, sizeof(s_art));
  unlock();
  return true;
}
bool ds_get_comp_art(uint8_t *dst, uint32_t dst_sz, uint8_t *w, uint8_t *h, uint16_t *seq, uint32_t ms) {
  if (!dst || !w || !h || !lock(ms)) return false;
  const uint8_t aw = s_data.comp_now.art_w, ah = s_data.comp_now.art_h;
  const uint32_t n = (uint32_t)aw * ah * 2;
  if (seq) *seq = s_data.comp_now.art_seq;
  const bool ok = n != 0 && n <= dst_sz;
  if (ok) memcpy(dst, s_art, n);
  *w = ok ? aw : 0;
  *h = ok ? ah : 0;
  unlock();
  return ok;
}

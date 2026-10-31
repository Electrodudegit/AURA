// ui_task.cpp - AURA v3.1 frame loop (core 1, sole SPI owner) + device HAL.
//
// Frame (25 fps, no delay() in the UI path):
//   process_input()  encoder + gesture queue + raw-touch ring -> ui_input()
//   ui_frame_begin() data snapshot, local time, screen tick
//   12 bands of 320x20: band n renders into buffer n&1 while band n-1 is
//   still going out by DMA. The 100 Hz touch tick runs between bands and in
//   the idle tail, always after the display DMA has drained.
// Theme colours come from theme_v3.h (via ui_core.h); no local redefinitions.
#include "prof.h"
#include "ui_task.h"
#include "config.h"
#include "version.h"
#include "aura_log.h"
#include "disp_ili9341.h"
#include "gfx.h"
#include "touch_xpt2046.h"
#include "touch_cal.h"
#include "gesture.h"
#include "encoder.h"
#include "ble_hid.h"
#include "data_store.h"
#include "net_task.h"
#include "wifi_mgr.h"
#include "secrets.h"
#include "ui_core.h"
#include "apps.h"
#include "tz.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static TaskHandle_t s_task = nullptr;
static volatile bool s_req_cal = false, s_req_grid = false, s_req_snap = false;
static TouchSample s_ts;
static uint32_t s_next_touch = 0;
static bool s_wdt = false;
static uint8_t s_enc_flags = 0;  // bit0 click, bit1 long (set in touch_service)
static uint32_t s_enc_sw_t = 0;
static UiData s_ui;
static uint32_t s_fps_t0 = 0, s_fps_frames = 0;
static uint32_t s_worst_ms = 0;  // max frame time since last ui_take_worst_ms()

// raw touch ring (calibration): filled by touch_service, drained before the frame
#define RAW_RING 32
static UiTouchRaw s_raw[RAW_RING];
static uint8_t s_raw_w = 0, s_raw_r = 0;

// [lat]: oldest unreflected input event handled this frame -> flush of this frame
static bool s_lat_pending = false;
static uint32_t s_lat_t0 = 0;
static const char *s_lat_src = "";
static const char *s_lat_ev = "";
static uint32_t s_lat_last_log = 0;

// prefs persistence (debounced)
static bool s_prefs_dirty = false;
static uint32_t s_prefs_t = 0;
static UiPrefs s_prefs_pending;

TaskHandle_t ui_task_handle(void) { return s_task; }
void ui_request_cal(void) { s_req_cal = true; }
void ui_request_grid(void) { s_req_grid = true; }
void ui_request_snap(void) { s_req_snap = true; }
uint32_t ui_take_worst_ms(void) { return __atomic_exchange_n(&s_worst_ms, 0u, __ATOMIC_RELAXED); }

static void lat_mark(uint32_t t_event, const char *src, const char *ev) {
  if (!s_lat_pending || (int32_t)(t_event - s_lat_t0) < 0) {
    s_lat_t0 = t_event;
    s_lat_src = src;
    s_lat_ev = ev;
  }
  s_lat_pending = true;
}
static const char *s_lat_tag = nullptr;  // v3.1.4: Buddy pop tag for the next [lat] line
void hal_lat_tag(const char *tag) { if (s_lat_pending) s_lat_tag = tag; }

// =================================================================== HAL ==
void hal_log(const char *fmt, ...) {
  char buf[AURA_LOG_MAX - 8];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  aura_log("%s", buf);
}
uint32_t hal_heap_free(void) { return heap_caps_get_free_size(HEAP_CAPS); }
uint32_t hal_heap_largest(void) { return heap_caps_get_largest_free_block(HEAP_CAPS); }
uint32_t hal_heap_min(void) { return heap_caps_get_minimum_free_size(HEAP_CAPS); }
uint32_t hal_uptime_s(void) { return millis() / 1000u; }
void hal_backlight(uint8_t pct) {
  static int16_t last = -1;
  if (pct > 100) pct = 100;
  const uint8_t lv = (uint8_t)(pct * 255u / 100u);
  if (lv == last) return;
  last = lv;
  disp_backlight(lv);
}
void hal_ble_enable(bool on) { ble_set_enabled(on); }
void hal_ble_kick(void) { ble_kick(); }
void hal_hid_consumer(uint16_t usage, uint8_t n) {
  if (n == 0) return;
  if (n > HID_MAX_REPEAT) n = HID_MAX_REPEAT;
  hid_send_consumer_n(usage, n);
}
void hal_hid_key(uint8_t mods, uint8_t key) { hid_send_key(mods, key); }
void hal_tz_apply(uint8_t idx) {
  tz_select(idx);
  configTzTime(tz_posix(tz_get()), "pool.ntp.org", "time.google.com");
}
void hal_comp_enable(bool on) { net_comp_enable(on); }
void hal_comp_forget(void) { net_comp_forget(); }
void hal_portal(bool on) { net_portal(on); }
void hal_net_refresh(int job) { net_request_refresh(job); }
bool hal_nvs_has_ssid(void) {
  char ssid[WIFI_SSID_MAX + 1];
  size_t plen = 0;
  ssid[0] = 0;
  return wifi_creds_peek(ssid, sizeof(ssid), &plen) && aura_cred_usable(ssid);
}
void hal_cred_query(HalCredQuery *q) {
  memset(q, 0, sizeof(*q));
  size_t plen = 0;
  wifi_creds_peek(q->ssid, sizeof(q->ssid), &plen);
  if (!aura_cred_usable(q->ssid)) q->ssid[0] = 0;
  q->pass_len = (q->ssid[0] && plen < 256) ? (uint8_t)plen : 0;
  net_key_lens(q->key_len);
}
int hal_cred_commit(const HalCredCommit *c) {
  if (!c) return 2;
  static const char *const names[6] = {"stocks", "cricket", "oracle", "groq", "owm", "gemini"};
  if (c->write_wifi) {
    if (!c->ssid || !c->pass || !wifi_creds_save(c->ssid, c->pass)) return 1;
    aura_log("[ui] credentials wifi saved ssid_len=%u pass_len=%u", (unsigned)strlen(c->ssid), (unsigned)strlen(c->pass));
  }
  unsigned set_n = 0, clr_n = 0;
  for (int i = 0; i < 6; i++) {
    if (c->key_op[i] == 1) {
      if (!c->key[i] || !aura_cred_usable(c->key[i]) || !net_key_set(names[i], c->key[i])) return 2;
      set_n++;
    } else if (c->key_op[i] == 2) {
      if (!net_key_set(names[i], "")) return 2;
      clr_n++;
    }
  }
  if (set_n || clr_n) aura_log("[ui] credentials keys set=%u cleared=%u", set_n, clr_n);
  if (c->write_wifi || set_n || clr_n) hal_net_refresh(-1);
  return 0;
}
uint32_t hal_oracle_ask(const char *prompt) { return net_oracle_ask(prompt); }

#define PREFS_MAGIC 0xA531u
struct PrefsBlob { uint16_t magic; uint16_t size; UiPrefs p; };
bool hal_prefs_load(UiPrefs *p) {
  Preferences pr;
  if (!pr.begin("ui", false)) return false;  // rw: one-shot brightness migration below
  PrefsBlob b;
  const size_t n = pr.getBytes("p", &b, sizeof(b));
  const bool ok = n == sizeof(b) && b.magic == PREFS_MAGIC && b.size == sizeof(UiPrefs);
  // v3.1.6-bl: through v3.1.6 the default was 80 % and NVS is preserved across app
  // flashes, so every unit keeps 80 forever. Lift an untouched 80 to 100 once; a
  // later user choice of 80 sticks because "blm" is then set.
  if (!pr.isKey("blm")) {
    if (ok && b.p.bright == 80) {
      b.p.bright = BL_BRIGHT_DEF;
      pr.putBytes("p", &b, sizeof(b));
    }
    pr.putUChar("blm", 1);
  }
  pr.end();
  if (!ok) return false;
  *p = b.p;
  return true;
}
void hal_prefs_save(const UiPrefs *p) {  // debounced: written 2 s after the last change
  s_prefs_pending = *p;
  s_prefs_dirty = true;
  s_prefs_t = millis();
}
static void prefs_flush(uint32_t now, bool force) {
  if (!s_prefs_dirty || (!force && now - s_prefs_t < 2000u)) return;
  s_prefs_dirty = false;
  PrefsBlob b = {PREFS_MAGIC, (uint16_t)sizeof(UiPrefs), s_prefs_pending};
  Preferences pr;
  bool ok = pr.begin("ui", false);
  if (ok) { ok = pr.putBytes("p", &b, sizeof(b)) == sizeof(b); pr.end(); }
  aura_log("[ui] prefs saved %s (menu=%s face=%u bright=%u night=%u pc_keys=%u tz=%u layer=%u comp=%u)",
           ok ? "ok" : "FAILED", b.p.menu_list ? "list" : "grid", (unsigned)b.p.face, (unsigned)b.p.bright,
           (unsigned)b.p.night_dim, (unsigned)b.p.pc_keys, (unsigned)b.p.tz, (unsigned)b.p.macro_layer,
           (unsigned)b.p.comp_on);
}

static uint32_t isqrt32(uint32_t v) {
  uint32_t r = 0, bit = 1u << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}
bool hal_cal_solve_save(const int32_t raw[4][2], const int16_t pts[4][2], int32_t *err_px) {
  int32_t nat[3][2];
  for (int i = 0; i < 3; i++) touch_rot_from_screen(TFT_ROTATION, pts[i][0], pts[i][1], &nat[i][0], &nat[i][1]);
  TouchCal c;
  bool ok = touch_cal_solve(raw, nat, &c);
  int32_t err = -1;
  if (ok) {
    int32_t nx, ny, sx, sy;
    touch_cal_apply(&c, raw[3][0], raw[3][1], &nx, &ny);
    touch_rot_to_screen(TFT_ROTATION, nx, ny, &sx, &sy);
    const int32_t dx = sx - pts[3][0], dy = sy - pts[3][1];
    err = (int32_t)isqrt32((uint32_t)(dx * dx + dy * dy));
    ok = err <= TOUCH_CAL_VERIFY_PX;
  }
  *err_px = err;
  aura_log("[cal] raw p0=%ld,%ld p1=%ld,%ld p2=%ld,%ld v=%ld,%ld solve=%s verify_err=%ldpx", (long)raw[0][0],
           (long)raw[0][1], (long)raw[1][0], (long)raw[1][1], (long)raw[2][0], (long)raw[2][1], (long)raw[3][0],
           (long)raw[3][1], ok ? "ok" : "fail", (long)err);
  if (!ok) return false;
  const bool saved = touch_cal_save(&c);
  touch_set_cal(&c, saved);
  aura_log("[cal] k=%ld,%ld,%ld,%ld,%ld,%ld (Q16) nvs=%s", (long)c.k[0], (long)c.k[1], (long)c.k[2], (long)c.k[3],
           (long)c.k[4], (long)c.k[5], saved ? "saved" : "SAVE FAILED");
  return true;
}
void hal_cal_restore_jump_reject(bool on) {
  touch_set_jump_reject(on);
  gesture_reset();
  xQueueReset(gesture_queue());
}
const char *hal_cal_source(void) { return touch_cal_from_nvs() ? "nvs" : "default"; }
void hal_snap(void) { s_req_snap = true; }
void hal_diag(HalDiag *d) {
  memset(d, 0, sizeof(*d));
  d->fps_x10 = s_ui.fps_x10;
  d->worst_ms = __atomic_load_n(&s_worst_ms, __ATOMIC_RELAXED);
  d->overruns = s_ui.overruns;
  d->band_fail = s_ui.band_fail;
  d->touch_bus = s_ui.touch_bus_skip;
  d->touch_spi = s_ui.touch_spi_err;
  d->lat_max = s_ui.lat_max_ms;
  TouchStats ts;
  touch_get_stats(&ts);
  d->t_reads = ts.reads;
  d->t_rej_z = ts.rej_z;
  d->t_rej_range = ts.rej_range;
  d->t_rej_spread = ts.rej_spread;
  d->t_rej_jump = ts.rej_jump;
  d->t_fallback = ts.fallback_reads;
  const TouchIrqStatus is = touch_irq_status();
  d->irq = is == TOUCH_IRQ_SEEN ? "SEEN" : is == TOUCH_IRQ_UNROUTED ? "UNROUTED" : "UNKNOWN";
  d->enc_raw = encoder_raw();
  d->enc_inv = encoder_invalid_transitions();
}
void hal_reboot(void) {
  prefs_flush(millis(), true);
  aura_log("[ui] restart requested from System");
  Serial.flush();
  esp_restart();
}

// ================================================================== snap ==
// Serial RGB565 dump, one band at a time so the UI keeps running: a band is
// copied into a temporary 12.8 KB heap buffer when rendered, then streamed as
// base64 lines whenever the UART TX buffer has room. Decode: tools/snap2png.py.
// Bands come from consecutive frames (animated screens may show seams).
static uint8_t *s_snap = nullptr;
static int8_t s_snap_band = -1;  // band being sent, -1 = idle
static bool s_snap_have = false;  // s_snap holds band s_snap_band
static uint32_t s_snap_off = 0;
static void snap_start(void) {
  if (s_snap) return;
  s_snap = (uint8_t *)heap_caps_malloc(DISP_BAND_PIXELS * 2, HEAP_CAPS);
  if (!s_snap) { aura_log("[snap] no memory (%u B)", (unsigned)(DISP_BAND_PIXELS * 2)); return; }
  s_snap_band = 0;
  s_snap_have = false;
  aura_log("[snap] begin w=%d h=%d fmt=rgb565be band_h=%d bands=%d screen=%s", DISP_W, DISP_H, UI_BAND_H,
           DISP_H / UI_BAND_H, ui_screen_name());
}
static void snap_abort(const char *why) {
  aura_log("[snap] blocked: %s (aborted at band %d)", why, (int)s_snap_band);
  heap_caps_free(s_snap);
  s_snap = nullptr;
  s_snap_band = -1;
  s_snap_have = false;
}
static void snap_capture(int band, const uint16_t *buf) {
  if (!s_snap || s_snap_have || band != s_snap_band) return;
  if (ui_secret_on_screen() || g_ui.d.portal.on) { snap_abort("secret"); return; }
  memcpy(s_snap, buf, DISP_BAND_PIXELS * 2);  // already big-endian (panel byte order)
  s_snap_have = true;
  s_snap_off = 0;
}
static void snap_pump(void) {
  if (!s_snap || !s_snap_have) return;
  static const char k64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint32_t total = DISP_BAND_PIXELS * 2;
  for (int lines = 0; lines < 4; lines++) {
    if (Serial.availableForWrite() < 128) return;
    char out[112];
    int o = snprintf(out, sizeof(out), "[snap] b=%d o=%lu ", (int)s_snap_band, (unsigned long)s_snap_off);
    const uint32_t n = (total - s_snap_off) < 57u ? (total - s_snap_off) : 57u;  // 57 B -> 76 chars
    for (uint32_t i = 0; i < n; i += 3) {
      const uint32_t a = s_snap[s_snap_off + i];
      const uint32_t b = i + 1 < n ? s_snap[s_snap_off + i + 1] : 0;
      const uint32_t c = i + 2 < n ? s_snap[s_snap_off + i + 2] : 0;
      const uint32_t v = (a << 16) | (b << 8) | c;
      out[o++] = k64[(v >> 18) & 63];
      out[o++] = k64[(v >> 12) & 63];
      out[o++] = i + 1 < n ? k64[(v >> 6) & 63] : '=';
      out[o++] = i + 2 < n ? k64[v & 63] : '=';
    }
    out[o] = 0;
    aura_log("%s", out);
    s_snap_off += n;
    if (s_snap_off >= total) {
      s_snap_have = false;
      if (++s_snap_band >= DISP_H / UI_BAND_H) {
        heap_caps_free(s_snap);
        s_snap = nullptr;
        s_snap_band = -1;
        aura_log("[snap] end");
      }
      return;
    }
  }
}

// ======================================================= UI heap caches ==
// v3.1.3: net_task asks the UI to drop its heap caches (HOME hero mask) before every TLS
// gate and holds new builds off until the session ends. The UI frees them at its next
// frame start (<= one frame) and acks; hal_cache_ok() refuses while the hold is set.
static volatile uint32_t s_cache_req = 0, s_cache_ack = 0, s_cache_freed = 0;
static volatile bool s_cache_hold = false;
static volatile bool s_cache_lend = true;  // false = HOME caches only (band pair stays)
static bool s_full_next = true;  // next frame draws every band (screen change, buffers swapped, start)
uint32_t ui_cache_release(uint32_t timeout_ms) { return ui_cache_release_ex(timeout_ms, true); }
uint32_t ui_cache_release_ex(uint32_t timeout_ms, bool lend_bands) {
  s_cache_lend = lend_bands;  // published by the seq_cst add below
  s_cache_hold = true;
  const uint32_t want = __atomic_add_fetch(&s_cache_req, 1u, __ATOMIC_SEQ_CST);
  const uint32_t t0 = millis();
  while (__atomic_load_n(&s_cache_ack, __ATOMIC_SEQ_CST) != want) {
    if (millis() - t0 >= timeout_ms) return UINT32_MAX;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return s_cache_freed;
}
void ui_cache_resume(void) { s_cache_hold = false; }
bool ui_cache_held(void) { return s_cache_hold; }
uint32_t ui_releasable_bytes(void) { return home_cache_bytes() + disp_bands_lendable(); }
static void cache_service(void) {
  // Leaving HOME (Oracle, menu, any app) drops its caches at once; HOME rebuilds them lazily.
  if (g_ui.scr != SCR_HOME && home_cache_bytes()) {
    const uint32_t n = home_release_caches();
    aura_log("[ui] left home: caches released %lu B (heap %lu largest %lu)", (unsigned long)n,
             (unsigned long)hal_heap_free(), (unsigned long)hal_heap_largest());
  }
  const uint32_t r = __atomic_load_n(&s_cache_req, __ATOMIC_SEQ_CST);
  if (r == s_cache_ack) {
    // Session over: take the second band buffer back (double-buffered DMA again).
    static uint32_t s_next_try = 0, s_restore_fails = 0;
    if (!s_cache_hold && !disp_band_buf(1) && (int32_t)(millis() - s_next_try) >= 0) {
      if (disp_bands_restore()) {
        s_full_next = true;
        aura_log("[ui] band buffers restored after %lu failed tries (heap %lu largest %lu)", (unsigned long)s_restore_fails,
                 (unsigned long)hal_heap_free(), (unsigned long)hal_heap_largest());
        s_restore_fails = 0;
      } else {
        s_next_try = millis() + 500u;
        // A failed restore keeps the UI single-buffered (half bands). Log it so fragmentation is visible.
        if (s_restore_fails++ % 20u == 0)
          aura_log("[ui] WARN band restore failed x%lu: need %u contiguous (heap %lu largest %lu) - UI stays half-band",
                   (unsigned long)s_restore_fails, (unsigned)(DISP_BAND_PIXELS * 4u), (unsigned long)hal_heap_free(),
                   (unsigned long)hal_heap_largest());
      }
    }
    return;
  }
  // TLS session: drop HOME caches. Lend the band pair only when this request asked for it.
  const uint32_t n = home_release_caches() + (s_cache_lend ? disp_bands_lend() : 0u);
  s_full_next = true;
  s_cache_freed = n;
  if (n) aura_log("[ui] caches released %lu B for TLS (heap %lu largest %lu)", (unsigned long)n,
                  (unsigned long)hal_heap_free(), (unsigned long)hal_heap_largest());
  __atomic_store_n(&s_cache_ack, r, __ATOMIC_SEQ_CST);
}
bool hal_cache_ok(uint32_t n) {
  if (s_cache_hold) return false;
  const uint32_t fr = hal_heap_free(), lg = hal_heap_largest();
  return fr >= n + UI_CACHE_RESERVE && lg >= n + 1024u;
}

// ===================================================== bench commands ==
// v3.1.3: serial bench commands (setup_cli) are queued here and run at the next frame start.
#define BENCH_Q 8
static volatile uint8_t s_bq_w = 0, s_bq_r = 0;
static uint8_t s_bq_op[BENCH_Q];
static int16_t s_bq_arg[BENCH_Q];
static portMUX_TYPE s_bq_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_prof_on = true;
void ui_prof_enable(bool on) { s_prof_on = on; }
bool ui_prof_enabled(void) { return s_prof_on; }
bool ui_bench(uint8_t op, int16_t arg) {
  taskENTER_CRITICAL(&s_bq_mux);
  const uint8_t nw = (uint8_t)((s_bq_w + 1) % BENCH_Q);
  const bool ok = nw != s_bq_r;
  if (ok) { s_bq_op[s_bq_w] = op; s_bq_arg[s_bq_w] = arg; s_bq_w = nw; }
  taskEXIT_CRITICAL(&s_bq_mux);
  return ok;
}
static void bench_service(uint32_t now) {
  while (s_bq_r != s_bq_w) {
    const uint8_t op = s_bq_op[s_bq_r];
    const int16_t arg = s_bq_arg[s_bq_r];
    s_bq_r = (uint8_t)((s_bq_r + 1) % BENCH_Q);
    g_ui.now = now;
    g_ui.last_input = now;  // a bench command counts as input (no idle takeover)
    if (g_ui.scr == SCR_BOOT) { aura_log("[bench] ignored during boot"); continue; }
    switch (op) {
      case UB_FACE_STEP:
        if (g_ui.scr != SCR_HOME) ui_go(SCR_HOME);
        home_face_step(arg);
        break;
      case UB_FACE_SET:
        if (g_ui.scr != SCR_HOME) ui_go(SCR_HOME);
        home_face_set((uint8_t)arg);
        break;
      case UB_MENU_OPEN: if (g_ui.scr != SCR_MENU) ui_go(SCR_MENU); break;
      case UB_MENU_CLOSE: if (g_ui.scr == SCR_MENU) ui_go(SCR_HOME); break;
      case UB_APP: ui_open_app((uint8_t)arg); break;
      case UB_BACK: ui_back(); break;
      case UB_HOME: ui_go(SCR_HOME); break;
      default: break;
    }
    aura_log("[bench] op=%u arg=%d -> screen %s face=%u", (unsigned)op, (int)arg, ui_screen_name(), (unsigned)g_ui.prefs.face);
  }
}

// ============================================================ touch tick ==
// v3.1.3: the 100 Hz tick no longer drains the display DMA between bands (that stall cost
// ~2.6 ms per tick, ~17 ms per frame on hardware). Between bands it only runs if the bus is
// already idle; otherwise it is deferred to the next idle point (frame flush / idle tail),
// at most TOUCH_MAX_DEFER_MS late, after which it waits for the bus as before.
#define TOUCH_MAX_DEFER_MS 30
static uint32_t s_touch_deferred = 0, s_touch_reads = 0;
static void touch_service(bool may_wait) {
  const uint32_t now = millis();
  if ((int32_t)(now - s_next_touch) < 0) return;
  if (!disp_poll_idle()) {
    if (!may_wait && (int32_t)(now - s_next_touch) < TOUCH_MAX_DEFER_MS) { s_touch_deferred++; return; }
    // Bus must be free before switching to the 2 MHz device; if the display DMA
    // did not drain, skip this touch read (state unchanged) rather than collide.
    if (!disp_wait_idle(50)) { s_ui.touch_bus_skip++; s_next_touch = now + TOUCH_TICK_MS; return; }
  }
  s_next_touch += TOUCH_TICK_MS;
  if ((int32_t)(now - s_next_touch) > 3 * TOUCH_TICK_MS) s_next_touch = now + TOUCH_TICK_MS;  // resync, no burst
  s_touch_reads++;
  // The KY-040 switch is GPIO, not SPI. A failed touch read must not drop the click.
  const bool touch_ok = touch_tick(now, &s_ts);
  if (!touch_ok) s_ui.touch_spi_err++;
  const EncEvent ee = encoder_poll_switch(now);
  if (ee == ENC_EV_CLICK) { s_enc_flags |= 1; s_enc_sw_t = now; }
  if (ee == ENC_EV_LONG) { s_enc_flags |= 2; s_enc_sw_t = now; }
  if (!touch_ok) return;
  if (ui_wants_raw_touch()) {
    const uint8_t nw = (uint8_t)((s_raw_w + 1) % RAW_RING);
    if (nw != s_raw_r) {
      UiTouchRaw &r = s_raw[s_raw_w];
      r.down = s_ts.down; r.fresh = s_ts.fresh; r.x = s_ts.x; r.y = s_ts.y; r.rx = s_ts.rx; r.ry = s_ts.ry; r.z = s_ts.z;
      s_raw_w = nw;
    }
  } else {
    gesture_feed(&s_ts, now);
  }
  snap_pump();
}

// ================================================================= input ==
static uint8_t map_gest(uint8_t g) {
  switch (g) {
    case GEST_TAP: return IN_TAP;
    case GEST_LONG: return IN_LONG;
    case GEST_SWIPE_L: return IN_SWL;
    case GEST_SWIPE_R: return IN_SWR;
    case GEST_SWIPE_U: return IN_SWU;
    case GEST_SWIPE_D: return IN_SWD;
    default: return IN_NONE;
  }
}
// v3.1.4: a click / long press or a gesture is waiting for process_input()
static bool input_pending(void) {
  if (s_enc_flags) return true;
  QueueHandle_t q = gesture_queue();
  return q && uxQueueMessagesWaiting(q) != 0;
}
static void process_input(uint32_t now) {
  if (s_req_cal) { s_req_cal = false; ui_go(SCR_CAL); }
  if (s_req_grid) { s_req_grid = false; ui_go(g_ui.scr == SCR_TGRID ? SCR_SETTINGS : SCR_TGRID); }
  if (s_req_snap) {
    s_req_snap = false;
    if (ui_secret_on_screen() || g_ui.d.portal.on) aura_log("[snap] blocked: secret on screen");
    else snap_start();
  }
  UiIn in;
  // encoder rotation, coalesced per frame
  uint32_t t_det = 0;
  const int32_t det = encoder_take_detents_ts(&t_det);
  if (det != 0) {
    lat_mark(t_det, "enc", det > 0 ? "CW" : "CCW");
    memset(&in, 0, sizeof(in));
    in.type = IN_ROT;
    in.d = (int16_t)(det > 50 ? 50 : det < -50 ? -50 : det);
    ui_input(in);
  }
  const uint8_t ef = s_enc_flags;
  s_enc_flags = 0;
  if (ef) {
    lat_mark(s_enc_sw_t, "enc", (ef & 2) ? "LONG" : "CLICK");
    memset(&in, 0, sizeof(in));
    if (ef & 1) { in.type = IN_CLICK; ui_input(in); }
    if (ef & 2) { in.type = IN_HOLD; ui_input(in); }
  }
  // raw touch samples (calibration)
  while (s_raw_r != s_raw_w) {
    ui_touch_raw(s_raw[s_raw_r]);
    s_raw_r = (uint8_t)((s_raw_r + 1) % RAW_RING);
  }
  // gestures
  GestEvent ev;
  while (xQueueReceive(gesture_queue(), &ev, 0) == pdTRUE) {
    const uint8_t t = map_gest(ev.type);
    if (t == IN_NONE) continue;
    lat_mark(ev.t_ms, "touch", gesture_name(ev.type));
    memset(&in, 0, sizeof(in));
    in.type = t;
    in.x = ev.x; in.y = ev.y; in.x0 = ev.x0; in.y0 = ev.y0;
    ui_input(in);
  }
  (void)now;
}

// ================================================================ render ==
// [prof] accumulators (one line per second, averages per frame; prof.h layers).
// Cost: two esp_timer reads per band plus two per layer scope (~1 us each).
#define NBANDS (DISP_H / UI_BAND_H)
static uint32_t s_pf_layer[PL_COUNT], s_pf_band[NBANDS], s_pf_comp = 0, s_pf_spi = 0, s_pf_tick = 0, s_pf_frames = 0;
static uint32_t s_pf_worst_frame = 0;
// v3.1.3 band keys (ui_core ui_band_key): a band whose key repeats is neither composed nor
// sent (the panel keeps its pixels); a band whose key repeats but whose window key changed
// is composed and sent only inside its column window (CASET). One static band per
// BAND_REFRESH_FRAMES is redrawn anyway (self-healing), and snaps/screen changes draw all.
#define BAND_REFRESH_FRAMES 16
static uint32_t s_bk[NBANDS], s_bwk[NBANDS];
static uint32_t s_pf_drawn = 0, s_pf_win = 0, s_pf_skip = 0, s_frame_no = 0;
void ui_force_full_frame(void) { s_full_next = true; }
static void render_frame(void) {
  prof_frame_reset();
  uint32_t spi = 0, comp = 0;
  const bool keys_ok = !s_snap && !s_full_next;
  s_full_next = false;
  s_frame_no++;
  const int force_b = (s_frame_no % BAND_REFRESH_FRAMES) == 0 ? (int)((s_frame_no / BAND_REFRESH_FRAMES) % NBANDS) : -1;
  int slot = 0;
  const bool dbl = disp_band_buf(1) != nullptr;  // false while the band pair is lent to TLS
  const int rows = disp_band_rows();             // 20, or 10 (half bands, single buffer) while lent
  for (int b = 0; b < NBANDS; b++) {
    BandWin w;
    const uint32_t key = keys_ok ? ui_band_key(b, &w) : 0;
    if (!keys_ok) { w.x0 = w.x1 = 0; w.key = 0; }
    const bool same = key && key == s_bk[b] && b != force_b;
    if (same && w.key == s_bwk[b]) {  // unchanged: nothing to compose or send
      s_pf_skip++;
      touch_service(false);
      continue;
    }
    const bool win = same && w.x1 > w.x0 && dbl;
    // Previous use of this buffer must be out of DMA; if not, skip this band
    // (never draw into a buffer the DMA may still be reading).
    bool sent = true;
    for (int y = b * UI_BAND_H; y < (b + 1) * UI_BAND_H; y += rows) {
      const uint32_t w0 = prof_now_us();
      const bool ok = disp_band_wait(slot, 50);
      const uint32_t w1 = prof_now_us();
      spi += w1 - w0;
      if (!ok) {
        s_ui.band_fail++;
        sent = false;
        break;
      }
      uint16_t *buf = disp_band_buf(slot);
      if (!buf) {  // never compose into a missing band buffer (lend/restore edge)
        s_ui.band_fail++;
        sent = false;
        break;
      }
      if (win) gfx_begin_win(buf, w.x0, y, w.x1 - w.x0, rows);
      else gfx_begin(buf, y, rows);
      ui_draw();
      const uint32_t w2 = prof_now_us();
      comp += w2 - w1;
      s_pf_band[b] += w2 - w1;
      if (!win && rows == UI_BAND_H) snap_capture(b, buf);
      const bool sb = win ? disp_win_send(slot, w.x0, y, w.x1 - w.x0, rows) : disp_band_send(slot, y, rows);
      if (!sb) { s_ui.band_fail++; sent = false; }
      spi += prof_now_us() - w2;
      if (dbl) slot ^= 1;
    }
    if (win) s_pf_win++; else s_pf_drawn++;
    s_bk[b] = sent ? key : 0;
    s_bwk[b] = sent ? w.key : 0;
    touch_service(false);  // runs only if the bus is already idle (or the tick is overdue)
  }
  // Flush: wait for the last band so "frame done" means pixels are on the panel.
  const uint32_t w3 = prof_now_us();
  if (!disp_wait_idle(50)) s_ui.band_fail++;
  spi += prof_now_us() - w3;
  touch_service(true);  // bus idle now: catch up a deferred tick
  uint32_t l[PL_COUNT];
  prof_get(l);
  for (int i = 0; i < PL_COUNT; i++) s_pf_layer[i] += l[i];
  s_pf_comp += comp;
  s_pf_spi += spi;
  s_pf_frames++;
  if (comp > s_pf_worst_frame) s_pf_worst_frame = comp;
}
// tick = ui_frame_begin (screen tick + caches), compose = ui_draw over 12 bands,
// spi = DMA waits + queueing (time compose had to wait for the bus), all us per frame.
static void prof_log(const char *scr) {
  if (!s_pf_frames) return;
  if (!s_prof_on) {
    memset(s_pf_layer, 0, sizeof(s_pf_layer));
    memset(s_pf_band, 0, sizeof(s_pf_band));
    s_pf_comp = s_pf_spi = s_pf_tick = s_pf_frames = s_pf_worst_frame = 0;
    s_pf_drawn = s_pf_win = s_pf_skip = s_touch_deferred = s_touch_reads = 0;
    return;
  }
  const uint32_t n = s_pf_frames;
  int wb = 0;
  for (int b = 1; b < NBANDS; b++) if (s_pf_band[b] > s_pf_band[wb]) wb = b;
  aura_log("[prof] scr=%s n=%lu tick=%lu compose=%lu spi=%lu worst_compose=%lu | other=%lu bg=%lu aurora=%lu tiles=%lu text=%lu part=%lu face=%lu | band_max=b%d:%lu"
           " | bands drawn=%lu win=%lu skip=%lu touch=%lu defer=%lu",
           scr, (unsigned long)n, (unsigned long)(s_pf_tick / n), (unsigned long)(s_pf_comp / n), (unsigned long)(s_pf_spi / n),
           (unsigned long)s_pf_worst_frame, (unsigned long)(s_pf_layer[PL_OTHER] / n), (unsigned long)(s_pf_layer[PL_BG] / n),
           (unsigned long)(s_pf_layer[PL_AURORA] / n), (unsigned long)(s_pf_layer[PL_TILES] / n), (unsigned long)(s_pf_layer[PL_TEXT] / n),
           (unsigned long)(s_pf_layer[PL_PART] / n), (unsigned long)(s_pf_layer[PL_FACE] / n), wb, (unsigned long)(s_pf_band[wb] / n),
           (unsigned long)(s_pf_drawn / n), (unsigned long)(s_pf_win / n), (unsigned long)(s_pf_skip / n),
           (unsigned long)s_touch_reads, (unsigned long)s_touch_deferred);
  s_pf_drawn = s_pf_win = s_pf_skip = s_touch_deferred = s_touch_reads = 0;
  memset(s_pf_layer, 0, sizeof(s_pf_layer));
  memset(s_pf_band, 0, sizeof(s_pf_band));
  s_pf_comp = s_pf_spi = s_pf_tick = s_pf_frames = s_pf_worst_frame = 0;
}
static void lat_flush(uint32_t now) {
  if (!s_lat_pending) return;
  s_lat_pending = false;
  const uint32_t lat = now - s_lat_t0;
  if (lat > s_ui.lat_max_ms) s_ui.lat_max_ms = (uint16_t)(lat > 65535 ? 65535 : lat);
  const char *tag = s_lat_tag;
  s_lat_tag = nullptr;
  if (!tag && (uint32_t)(now - s_lat_last_log) < LAT_LOG_MIN_MS) { s_ui.lat_suppressed++; return; }
  s_lat_last_log = now;
  s_ui.lat_logged++;
  if (tag) aura_log("[lat] src=%s ev=%s in->flush=%lums pop=%s", s_lat_src, s_lat_ev, (unsigned long)lat, tag);
  else aura_log("[lat] src=%s ev=%s in->flush=%lums", s_lat_src, s_lat_ev, (unsigned long)lat);
}
static_assert(DISP_H % UI_BAND_H == 0, "band height must divide 240");
static_assert(UI_BAND_H == 20, "ui_core screens assume 12 bands of 20 rows");

static bool local_time(struct tm *lt) {
  const time_t t = time(nullptr);
  localtime_r(&t, lt);
  return lt->tm_year >= (2024 - 1900);
}

// ================================================================== task ==
static void ui_task(void *) {
  if (!disp_init()) aura_log("[ui] display init FAILED");
  if (!touch_init()) aura_log("[ui] touch init FAILED");
  aura_log("[ui] touch cal source=%s", touch_cal_from_nvs() ? "nvs" : "default (p2 factory values)");
  if (!gesture_init()) aura_log("[ui] gesture queue FAILED");
  // Task watchdog (ESP-IDF): subscribe this task; init the TWDT if the core did not.
  esp_err_t we = esp_task_wdt_add(nullptr);
  if (we == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_config_t cfg = {};
    cfg.timeout_ms = 5000;
    cfg.idle_core_mask = 0;
    cfg.trigger_panic = true;
    if (esp_task_wdt_init(&cfg) == ESP_OK) we = esp_task_wdt_add(nullptr);
  }
  s_wdt = (we == ESP_OK);
  aura_log("[ui] task wdt %s", s_wdt ? "subscribed" : "unavailable");

  const uint32_t t_start = millis();
  memset(&s_ui, 0, sizeof(s_ui));
  ui_init(t_start);
  ds_get(&g_ui.d, 20);  // v3.1.3: snapshot straight into g_ui (the separate 1.7 KB s_d copy is gone)
  aura_log("[ui] prefs menu=%s face=%u bright=%u pc_keys=%s", g_ui.prefs.menu_list ? "list" : "grid",
           (unsigned)g_ui.prefs.face, (unsigned)g_ui.prefs.bright, g_ui.prefs.pc_keys ? "ON" : "OFF");
  s_next_touch = t_start;
  s_fps_t0 = t_start;
  TickType_t last = xTaskGetTickCount();
  struct tm lt;
  const char *last_scr = "";

  for (;;) {
    const uint32_t f0 = millis();
    if (s_wdt) esp_task_wdt_reset();
    ds_get(&g_ui.d, 2);  // on timeout keep the previous snapshot
    const bool tv = local_time(&lt);
    process_input(f0);
    const uint32_t tk0 = prof_now_us();
    cache_service();
    bench_service(f0);
    ui_frame_begin(f0, nullptr, &lt, tv);
    s_pf_tick += prof_now_us() - tk0;
    const char *sn = ui_screen_name();
    if (sn != last_scr) { s_full_next = true; aura_log("[ui] screen %s heap=%lu largest=%lu", sn, (unsigned long)hal_heap_free(), (unsigned long)hal_heap_largest()); last_scr = sn; }
    render_frame();
    const uint32_t f1 = millis();
    lat_flush(f1);
    prefs_flush(f1, false);
    const uint32_t fms = f1 - f0;

    s_ui.frames++;
    s_fps_frames++;
    uint32_t w = __atomic_load_n(&s_worst_ms, __ATOMIC_RELAXED);
    while (fms > w && !__atomic_compare_exchange_n(&s_worst_ms, &w, fms, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    if (fms > UI_FRAME_CAP_MS) s_ui.overruns++;
    if ((uint32_t)(f0 - s_fps_t0) >= 1000u) {
      s_ui.fps_x10 = (uint16_t)((s_fps_frames * 10000u) / (uint32_t)(f0 - s_fps_t0));
      s_fps_t0 = f0;
      s_fps_frames = 0;
      s_ui.spi_timeouts = disp_spi_timeouts();
      ds_set_ui(&s_ui, 2);
      prof_log(sn);
    }

    // Idle tail: keep the 100 Hz touch tick alive until the frame deadline.
    // Buddy still wakes the next frame at once. A slower idle period does too,
    // so a tap is not stuck behind a 250 ms gap. Light sleep is only the gap
    // between touch polls, and only when ui_light_sleep_ok() says the radios
    // are down. A failure is logged once; dimming and the slower period remain.
    const uint32_t period = ui_frame_period_ms();
    const bool pop_wake = buddy_on_screen();
    bool early = false;
    const TickType_t deadline = last + pdMS_TO_TICKS(period);
    while (!early) {
      const TickType_t nowt = xTaskGetTickCount();
      if ((int32_t)(deadline - nowt) <= 0) break;
      const int32_t to_touch = (int32_t)(s_next_touch - millis());
      const int32_t to_deadline = (int32_t)pdTICKS_TO_MS(deadline - nowt);
      if (to_touch >= to_deadline) break;
      if (ui_light_sleep_ok() && to_touch > 20 && to_deadline > 20) {
        uint32_t ms = (uint32_t)(to_touch < 30 ? to_touch : 20);
        if ((int32_t)ms > to_deadline) ms = (uint32_t)to_deadline;
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ull);
        const esp_err_t se = esp_light_sleep_start();
        if (s_wdt) esp_task_wdt_reset();
        if (se != ESP_OK) {
          static bool logged = false;
          if (!logged) {
            aura_log("[ui] light sleep unavailable (0x%x); dim and slower frames still apply", (unsigned)se);
            logged = true;
          }
          vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
        }
      } else if (to_touch > 0) {
        vTaskDelay(pdMS_TO_TICKS(to_touch) ? pdMS_TO_TICKS(to_touch) : 1);
      }
      touch_service(true);
      early = input_pending() && (pop_wake || period > UI_FRAME_MS);
    }
    if (early) {
      vTaskDelay(1);  // 1 tick: still yield to lower-priority work on this core
      last = xTaskGetTickCount();
      continue;
    }
    if (xTaskDelayUntil(&last, pdMS_TO_TICKS(period)) == pdFALSE) {
      const TickType_t nowt = xTaskGetTickCount();
      if ((int32_t)(nowt - last) >= (int32_t)pdMS_TO_TICKS(period)) {
        last = nowt;    // more than a frame late: drop the backlog instead of bursting
        vTaskDelay(1);  // and yield so lower-priority work on core 1 (loop/CLI) runs
      }
    }
  }
}

bool ui_task_start(void) {
  // Stack + TCB come from the heap ONCE at boot (not steady state).
  if (xTaskCreatePinnedToCore(ui_task, "ui", UI_TASK_STACK, nullptr, UI_TASK_PRIO, &s_task, UI_TASK_CORE) != pdPASS) s_task = nullptr;
  return s_task != nullptr;
}

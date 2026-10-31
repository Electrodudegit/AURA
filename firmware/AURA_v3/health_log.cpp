#include "health_log.h"
#include "config.h"
#include "aura_log.h"
#include "data_store.h"
#include "ui_task.h"
#include "net_task.h"
#include "ble_hid.h"
#include "touch_xpt2046.h"
#include "gesture.h"
#include "disp_ili9341.h"
#include "encoder.h"
#include "net_proto.h"
#include "apps.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

static uint32_t s_next = 0;

// ---- failed allocation hook (ESP-IDF heap_caps_register_failed_alloc_callback)
// Runs in the context of the failing allocation (any task, inside the
// allocator): only record into statics, never log or allocate here.
static volatile uint32_t s_af_count = 0;
static volatile uint32_t s_af_size = 0;
static volatile uint32_t s_af_caps = 0;
static const char *volatile s_af_fn = nullptr;
static uint32_t s_af_reported = 0;

static void IRAM_ATTR alloc_failed_hook(size_t size, uint32_t caps, const char *fn) {
  s_af_size = (uint32_t)size;
  s_af_caps = caps;
  s_af_fn = fn;  // string literal (__func__) in flash/rodata, safe to keep the pointer
  __atomic_fetch_add(&s_af_count, 1u, __ATOMIC_RELAXED);
}

bool health_init(void) { return heap_caps_register_failed_alloc_callback(alloc_failed_hook) == ESP_OK; }

// ESP-IDF: uxTaskGetStackHighWaterMark() returns BYTES on ESP32 (StackType_t = uint8_t).
static unsigned hwm(TaskHandle_t t) { return t ? (unsigned)uxTaskGetStackHighWaterMark(t) : 0u; }

static const char *wifi_str(uint8_t st) {
  switch (st) {
    case WIFI_ST_UP: return "UP";
    case WIFI_ST_CONNECTING: return "CONN";
    case WIFI_ST_BACKOFF: return "BACKOFF";
    default: return "NOCREDS";
  }
}

// v3.1.3: one AuraData snapshot buffer for the serial CLI and the health log. Both run only in
// the ble task (loopTask is gone) and never nest a snapshot, so one 1.7 KB copy is enough.
AuraData *health_scratch(void) {
  static AuraData s_scratch;
  return &s_scratch;
}

void health_print_now(void) {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;  // no PSRAM; be explicit anyway
  const uint32_t heap = heap_caps_get_free_size(caps);
  const uint32_t largest = heap_caps_get_largest_free_block(caps);
  const uint32_t minh = heap_caps_get_minimum_free_size(caps);
  const uint32_t worst = ui_take_worst_ms();  // max since the previous [health] line
  AuraData &d = *health_scratch();  // v3.1.3: shared static snapshot (ble task only)
  if (!ds_get(&d, 20)) memset(&d, 0, sizeof(d));
  aura_log("[health] up=%lus heap=%lu largest=%lu min=%lu fps=%u.%u hwm ui=%u net=%u ble=%u evt=%u wifi=%s(%d) "
           "ble_conn=%u ovr=%lu worst=%lums spi_to=%lu enc_inv=%lu",
           (unsigned long)(millis() / 1000u), (unsigned long)heap, (unsigned long)largest, (unsigned long)minh,
           (unsigned)(d.ui.fps_x10 / 10), (unsigned)(d.ui.fps_x10 % 10), hwm(ui_task_handle()),
           hwm(net_task_handle()), hwm(ble_task_handle()), hwm(xTaskGetHandle("arduino_events")),
           wifi_str(d.net.wifi_state), (int)d.net.rssi, (unsigned)d.ble.connected, (unsigned long)d.ui.overruns,
           (unsigned long)worst, (unsigned long)d.ui.spi_timeouts, (unsigned long)encoder_invalid_transitions());
  TouchStats ts;
  touch_get_stats(&ts);
  const TouchIrqStatus irq = touch_irq_status();
  aura_log("[health] fail band=%lu touch_bus=%lu touch_spi=%lu tls_skip=%lu alloc_fail=%lu irq=%s fb_reads=%lu "
           "lat_max=%ums",
           (unsigned long)d.ui.band_fail, (unsigned long)d.ui.touch_bus_skip, (unsigned long)d.ui.touch_spi_err,
           (unsigned long)d.net.tls_skipped, (unsigned long)s_af_count,
           irq == TOUCH_IRQ_SEEN ? "seen" : irq == TOUCH_IRQ_UNROUTED ? "unrouted" : "unknown",
           (unsigned long)ts.fallback_reads, (unsigned)d.ui.lat_max_ms);
  const uint32_t afc = s_af_count;
  if (afc != s_af_reported) {
    s_af_reported = afc;
    const char *fn = s_af_fn;
    aura_log("[health] WARN alloc failed count=%lu last size=%lu caps=0x%lx fn=%s", (unsigned long)afc,
             (unsigned long)s_af_size, (unsigned long)s_af_caps, fn ? fn : "?");
  }
  // v3.1.3: per-host floor; UI caches are released before a gate, so they count as free.
  const uint32_t avail = heap + ui_releasable_bytes();
  const uint32_t need_or = tls_floor(GROQ_HOST), need_wx = tls_floor(WEATHER_HOST);
  if (largest < NET_TLS_MIN_LARGEST || avail < need_wx)
    aura_log("[health] WARN TLS gate: free %lu (+ui release %lu) largest %lu | need oracle(groq) %lu %s, weather %lu %s, largest %u",
             (unsigned long)heap, (unsigned long)ui_releasable_bytes(), (unsigned long)largest, (unsigned long)need_or,
             avail >= need_or ? "ok" : "CLOSED", (unsigned long)need_wx, avail >= need_wx ? "ok" : "CLOSED",
             (unsigned)NET_TLS_MIN_LARGEST);
  if (d.net.tls_largest_min != UINT32_MAX && d.net.tls_largest_min < HEALTH_FREE_FLOOR)
    aura_log("[health] WARN TLS-window largest free block %lu < %u", (unsigned long)d.net.tls_largest_min,
             (unsigned)HEALTH_FREE_FLOOR);
}

void health_poll(uint32_t now) {
  if ((int32_t)(now - s_next) < 0) return;
  s_next = now + HEALTH_PERIOD_MS;
  health_print_now();
}

// ---- v3.1.3 `heap` bench command ---------------------------------------------
struct TopWalk { uint32_t t[3]; uint32_t nfree, nused; };
static bool top_walk_cb(walker_heap_into_t, walker_block_info_t bi, void *u) {
  TopWalk *w = (TopWalk *)u;
  if (bi.used) { w->nused++; return true; }
  w->nfree++;
  uint32_t v = (uint32_t)bi.size;
  for (int i = 0; i < 3; i++)
    if (v > w->t[i]) { const uint32_t x = w->t[i]; w->t[i] = v; v = x; }
  return true;
}
static void cap_line(const char *name, uint32_t caps) {
  multi_heap_info_t mi;
  heap_caps_get_info(&mi, caps);
  aura_log("[heap] %-9s free=%lu largest=%lu min_ever=%lu alloc=%lu blocks used=%lu free=%lu", name,
           (unsigned long)mi.total_free_bytes, (unsigned long)mi.largest_free_block, (unsigned long)mi.minimum_free_bytes,
           (unsigned long)mi.total_allocated_bytes, (unsigned long)mi.allocated_blocks, (unsigned long)mi.free_blocks);
}
void health_heap_dump(void) {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  aura_log("[heap] free=%lu largest=%lu min_ever=%lu ui_releasable=%lu held=%d band_slots=%d",
           (unsigned long)heap_caps_get_free_size(caps), (unsigned long)heap_caps_get_largest_free_block(caps),
           (unsigned long)heap_caps_get_minimum_free_size(caps), (unsigned long)ui_releasable_bytes(), ui_cache_held() ? 1 : 0,
           disp_band_buf(1) ? 2 : 1);
  cap_line("int8", caps);
  cap_line("dma", MALLOC_CAP_DMA);
  cap_line("int32", MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
  cap_line("default", MALLOC_CAP_DEFAULT);
  TopWalk w = {{0, 0, 0}, 0, 0};
  heap_caps_walk(caps, top_walk_cb, &w);
  aura_log("[heap] top free blocks %lu %lu %lu (free blocks %lu, used %lu)", (unsigned long)w.t[0], (unsigned long)w.t[1],
           (unsigned long)w.t[2], (unsigned long)w.nfree, (unsigned long)w.nused);
  // Preview with what the UI would release before a gate (the blocks may not merge: free only).
  const uint32_t fr = heap_caps_get_free_size(caps) + ui_releasable_bytes();
  static const char *const hosts[] = {GROQ_HOST, ORACLE_HOST, WEATHER_HOST, AIR_HOST, CRYPTO_HOST, NEWS_HOST};
  for (const char *h : hosts) {
    const GateRes r = gate_decide(w.t[0], w.t[1], fr, tls_floor(h));
    aura_log("[heap] gate %-31s floor=%lu -> %s", h, (unsigned long)tls_floor(h), gate_name(r));
  }
  static const char *const tn[] = {"ui", "net", "ble", "arduino_events", "tcpip_thread", "wifi", "nimble_host", "btController",
                                   "esp_timer", "sys_evt"};
  char b[200];
  int o = snprintf(b, sizeof(b), "[heap] stack free (HWM bytes):");
  for (const char *n : tn) {
    TaskHandle_t t = xTaskGetHandle(n);
    if (t && o < (int)sizeof(b) - 24) o += snprintf(b + o, sizeof(b) - o, " %s=%u", n, hwm(t));
  }
  aura_log("%s", b);
}

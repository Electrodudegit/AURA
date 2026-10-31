// net_task.cpp - AURA v3.1 network task (core 0).
//
// * wifi_mgr (STA, backoff) + SNTP (timezone from Settings, default IST).
// * Job scheduler: WX (Open-Meteo forecast), AIR (Open-Meteo air quality),
//   CRYPTO (CoinGecko simple price), NEWS (BBC RSS), STOCKS (Twelve Data),
//   CRICKET (cricapi, streamed). One TLS session at a
//   time, each gated by the v3.1.1 heap walk (one >= 36 KB block, or two
//   >= 17 KB blocks, plus the 51200 B free floor) with doubling skip backoff. BLE is never paused or stopped for a
//   fetch: a connected HID link just costs heap, and the gate decides.
// * HTTP/1.0 requests (no chunked bodies). JSON bodies go into one bounded
//   4 KB buffer; RSS is parsed as a stream (512 B chunks) and the connection
//   is closed after NEWS_MAX titles.
// * Setup portal: SoftAP "AURA-Setup-XXXX" (random per-session WPA2 password
//   shown on the device screen only), DNSServer (captive) + WebServer form.
//   Saves Wi-Fi, API keys and location to NVS, then stops the AP. There is no
//   /update (OTA) endpoint. Secrets are never logged or echoed back.
// * v3.1.2: built-in key defaults from secrets.h (NVS wins); Oracle can use xAI or
//   Groq (both OpenAI-compatible), resolved by or_resolve().
// TLS uses NetworkClientSecure with the ESP-IDF CA bundle (certificates verified).
#include "net_task.h"
#include "config.h"
#include "aura_log.h"
#include "data_store.h"
#include "wifi_mgr.h"
#include "version.h"
#include "ble_hid.h"
#include "net_proto.h"
#include "ui_task.h"   // v3.1.3 UI cache release around TLS
#include "secrets.h"  // v3.1.2 built-in key defaults (NVS overrides); values are never logged
#ifndef GEMINI_API_KEY_DEFAULT  // older private secrets.h without the v3.1.6 Gemini line
#define GEMINI_API_KEY_DEFAULT ""
#endif
#include "tz.h"
#include "comp_proto.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum NetErr : int32_t {
  NET_OK = 0, NET_E_CONNECT = -1, NET_E_WRITE = -2, NET_E_TIMEOUT = -3, NET_E_STATUS = -4,
  NET_E_PARSE = -5, NET_E_NOBODY = -6, NET_E_HEAP = -7, NET_E_TOOBIG = -8, NET_E_PREEMPT = -9,
  // v3.1.6 p0p1: the API refused the key / a quota (HTTP 401/403/429, or Alpha Vantage's 200 + "Error Message" /
  // "Information"; ji.http then carries a synthetic 401 / 429 so the UI can say why). No fast retries.
  NET_E_KEY = -10, NET_E_RATE = -11,
};

#define NET_HEAP_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static TaskHandle_t s_task = nullptr;
static NetworkClientSecure s_tls;
static char s_body[NET_BODY_MAX];
static size_t s_body_n = 0;
static bool s_body_over = false;
static uint8_t s_chunk[512];
static volatile uint32_t s_refresh_mask = 0;
static volatile int8_t s_portal_req = -1;  // -1 none, 0 stop, 1 start

// Oracle request hand-off (UI -> net): prompt copied under a spinlock.
static portMUX_TYPE s_or_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_or_prompt[ORACLE_PROMPT_MAX];
static volatile uint32_t s_or_req_seq = 0;
static uint32_t s_or_done_seq = 0;
static inline bool oracle_pending(void) { return s_or_req_seq != s_or_done_seq; }
uint32_t net_oracle_ask(const char *prompt) {
  taskENTER_CRITICAL(&s_or_mux);
  strncpy(s_or_prompt, prompt, sizeof(s_or_prompt) - 1);
  s_or_prompt[sizeof(s_or_prompt) - 1] = 0;
  const uint32_t q = s_or_req_seq + 1u;
  s_or_req_seq = q;
  taskEXIT_CRITICAL(&s_or_mux);
  return q;
}

TaskHandle_t net_task_handle(void) { return s_task; }
void net_request_refresh(int job) {
  const uint32_t m = job < 0 ? ((1u << JOB_COUNT) - 1u) : (1u << (job & 7));
  __atomic_or_fetch(&s_refresh_mask, m, __ATOMIC_RELAXED);
}
void net_request_weather_now(void) { net_request_refresh(JOB_WX); }
void net_portal(bool on) { s_portal_req = on ? 1 : 0; }

// ------------------------------------------------------------ settings ---
// NVS "keys" names (v3.1.2 adds "groq" and "owm"; no renames). "oracle" is the xAI key.
// P016: "football" removed (app gone). NVS names of the others are unchanged; a stale "football"
// row from older builds is dropped by "Remove all saved keys" (k_key_legacy).
// v3.1.6 p0p1: "gemini" appended (Google Gemini Oracle key; NVS wins over GEMINI_API_KEY_DEFAULT).
#define NKEYS 6
enum { KI_STOCKS = 0, KI_CRICKET, KI_XAI, KI_GROQ, KI_OWM, KI_GEMINI };
static const char *const k_key_names[NKEYS] = {"stocks", "cricket", "oracle", "groq", "owm", "gemini"};
static const char k_key_legacy[] = "football";
// Built-in defaults (secrets.h), same order. Only ever strlen()'d or copied into a
// request buffer that is wiped right after use; never logged, shown or sent to the UI.
static const char *const k_key_dflt[NKEYS] = {STOCK_API_KEY_DEFAULT, CRICKET_API_KEY_DEFAULT,
                                              XAI_API_KEY_DEFAULT,   GROQ_API_KEY_DEFAULT,    OWM_API_KEY_DEFAULT,
                                              GEMINI_API_KEY_DEFAULT};
#define KEY_MAX 128
static volatile bool s_keys_dirty = true, s_loc_dirty = true;
static bool s_have_stocks = false, s_have_cricket = false;
static void wipe(void *p, size_t n);

// NVS value if present, else the built-in default. out must hold KEY_MAX + 1 bytes;
// the caller wipes it (volatile) after use. *src: 0 none, 1 nvs, 2 default.
static size_t key_load(int i, char *out, uint8_t *src) {
  out[0] = 0;
  uint8_t s = 0;
  Preferences p;
  if (p.begin("keys", true)) {
    if (p.isKey(k_key_names[i])) p.getString(k_key_names[i], out, KEY_MAX + 1);
    p.end();
  }
  if (out[0] && !aura_cred_usable(out)) out[0] = 0;  // placeholder is not a key
  if (out[0]) s = 1;
  else if (aura_cred_usable(k_key_dflt[i])) {
    strncpy(out, k_key_dflt[i], KEY_MAX);
    out[KEY_MAX] = 0;
    s = 2;
  }
  if (src) *src = s;
  return strnlen(out, KEY_MAX + 1);
}
static size_t key_len(int i, uint8_t *src) {  // length only (portal "set (len N)")
  char k[KEY_MAX + 1];
  const size_t l = key_load(i, k, src);
  wipe(k, sizeof(k));
  return l;
}
void net_key_lens(uint8_t out[6]) {
  for (int i = 0; i < NKEYS && i < 6; i++) {
    uint8_t src = 0;
    const size_t l = key_len(i, &src);
    out[i] = (src == 1 && l < 256) ? (uint8_t)l : 0;  // NVS only; placeholder is not "saved"
  }
}

bool net_key_set(const char *name, const char *value) {
  bool known = false;
  for (int i = 0; i < NKEYS; i++) known |= strcmp(name, k_key_names[i]) == 0;
  if (!known || strlen(value) > KEY_MAX) return false;
  Preferences p;
  if (!p.begin("keys", false)) return false;
  bool ok;
  if (value[0] == '\0') ok = p.remove(name) || !p.isKey(name);
  else ok = p.putString(name, value) == strlen(value);
  p.end();
  s_keys_dirty = true;
  return ok;
}
static void keys_publish(void) {
  KeysData k = {};
  Preferences p;
  if (p.begin("keys", true)) {
    // presence only: the value is never read into RAM here
    bool *const pr[NKEYS] = {&k.stocks, &k.cricket, &k.oracle, &k.groq, &k.owm, &k.gemini};
    for (int i = 0; i < NKEYS; i++) {
      const bool nv = p.isKey(k_key_names[i]);
      const bool dflt = aura_cred_usable(k_key_dflt[i]);
      *pr[i] = nv || dflt;
      if (!nv && dflt) k.dflt |= (uint8_t)(1u << i);
    }
    p.end();
  } else {
    bool *const pr[NKEYS] = {&k.stocks, &k.cricket, &k.oracle, &k.groq, &k.owm, &k.gemini};
    for (int i = 0; i < NKEYS; i++) if (aura_cred_usable(k_key_dflt[i])) { *pr[i] = true; k.dflt |= (uint8_t)(1u << i); }
  }
  ds_set_keys(&k, 50);
  s_have_stocks = k.stocks;
  s_have_cricket = k.cricket;
  aura_log("[net] keys present: stocks=%d cricket=%d xai=%d groq=%d owm=%d gemini=%d (default mask 0x%02x)", k.stocks,
           k.cricket, k.oracle, k.groq, k.owm, k.gemini, (unsigned)k.dflt);
}
static LocData s_loc;
static void loc_load(void) {
  s_loc.lat = LOC_DEFAULT_LAT;
  s_loc.lon = LOC_DEFAULT_LON;
  snprintf(s_loc.city, sizeof(s_loc.city), "%s", LOC_DEFAULT_CITY);
  Preferences p;
  if (p.begin("loc", true)) {
    if (p.isKey("lat") && p.isKey("lon")) {
      s_loc.lat = p.getFloat("lat", LOC_DEFAULT_LAT);
      s_loc.lon = p.getFloat("lon", LOC_DEFAULT_LON);
      p.getString("city", s_loc.city, sizeof(s_loc.city));
    }
    p.end();
  }
  ds_set_loc(&s_loc, 50);
  aura_log("[net] location %.2f,%.2f '%s'", (double)s_loc.lat, (double)s_loc.lon, s_loc.city);
}
static bool loc_save(float lat, float lon, const char *city) {
  Preferences p;
  if (!p.begin("loc", false)) return false;
  bool ok = p.putFloat("lat", lat) && p.putFloat("lon", lon);
  ok = ok && p.putString("city", city) == strlen(city);
  p.end();
  s_loc_dirty = true;
  return ok;
}

// ---- heap low-point sampling during a TLS session (p2) -------------------
// (a) esp_timer (task dispatch) samples free/largest every NET_HEAP_SAMPLE_US
// while the session is open, including the handshake; (b) the exact free low
// comes from heap_caps_get_minimum_free_size() when it dropped in the window.
static esp_timer_handle_t s_heap_timer = nullptr;
static volatile uint32_t s_smp_largest_min = UINT32_MAX;
static volatile uint32_t s_smp_free_min = UINT32_MAX;
static uint32_t s_skip_backoff = NET_TLS_SKIP_BACKOFF_MIN_MS;

static void heap_sample_cb(void *) {
  const uint32_t lg = heap_caps_get_largest_free_block(NET_HEAP_CAPS);
  const uint32_t fr = heap_caps_get_free_size(NET_HEAP_CAPS);
  if (lg < s_smp_largest_min) s_smp_largest_min = lg;
  if (fr < s_smp_free_min) s_smp_free_min = fr;
}
struct HeapWindow { uint32_t min_before; uint32_t largest; uint32_t freeh; };
static void heap_window_begin(HeapWindow *w) {
  w->min_before = heap_caps_get_minimum_free_size(NET_HEAP_CAPS);
  s_smp_largest_min = UINT32_MAX;
  s_smp_free_min = UINT32_MAX;
  heap_sample_cb(nullptr);
  if (s_heap_timer) esp_timer_start_periodic(s_heap_timer, NET_HEAP_SAMPLE_US);
}
static void heap_window_end(HeapWindow *w, NetData *n) {
  ui_cache_resume();  // session over: the UI may rebuild its caches
  if (s_heap_timer) esp_timer_stop(s_heap_timer);
  heap_sample_cb(nullptr);
  const uint32_t min_after = heap_caps_get_minimum_free_size(NET_HEAP_CAPS);
  w->largest = s_smp_largest_min;
  w->freeh = s_smp_free_min;
  if (min_after < w->min_before && min_after < w->freeh) w->freeh = min_after;
  if (w->largest < n->tls_largest_min) n->tls_largest_min = w->largest;
  if (w->freeh < n->tls_heap_min) n->tls_heap_min = w->freeh;
}

// ---- TLS gate (v3.1.3) ------------------------------------------------------
// 1) ask the UI to drop its heap caches (hero mask) and hold them off until the session ends;
// 2) walk the internal 8-bit heap for the two largest free blocks: a session may start if the
//    largest is >= 36 KB (single) or two distinct blocks are each >= 17 KB (mbedTLS IN/OUT
//    record buffers are separate allocations), and total free >= the measured need of this
//    host (net_proto tls_floor: handshake peak + Wi-Fi RX reserve + margin).
// The UI hold is released by heap_window_end() / a skipped gate / the job dispatcher.
struct GateWalk { uint32_t a, b; };
static bool gate_walk_cb(walker_heap_into_t, walker_block_info_t bi, void *u) {
  if (!bi.used) { GateWalk *g = (GateWalk *)u; top2_add((uint32_t)bi.size, &g->a, &g->b); }
  return true;  // heap lock held: no logging in here
}
// Two-phase release. Phase 1 drops HOME caches only (band pair stays double-buffered).
// The band pair is lent only if the same gate rule still fails. Admission is unchanged.
// Revert with -DNET_LEND_ON_DEMAND=0.
#ifndef NET_LEND_ON_DEMAND
#define NET_LEND_ON_DEMAND 1
#endif
static GateRes tls_gate(const char *host, uint32_t *largest, uint32_t *second, uint32_t *freeh, uint32_t *floor_out = nullptr) {
  const uint32_t fl = tls_floor(host);
  if (floor_out) *floor_out = fl;
  uint32_t rel = ui_cache_release_ex(NET_UI_RELEASE_MS, !NET_LEND_ON_DEMAND);
  GateWalk g = {0, 0};
  heap_caps_walk(NET_HEAP_CAPS, gate_walk_cb, &g);
  *freeh = (uint32_t)heap_caps_get_free_size(NET_HEAP_CAPS);
  GateRes r = gate_decide(g.a, g.b, *freeh, fl);
  bool lent = !NET_LEND_ON_DEMAND;
  if (NET_LEND_ON_DEMAND && !gate_ok(r) && ui_releasable_bytes() > 0) {
    const uint32_t rel2 = ui_cache_release_ex(NET_UI_RELEASE_MS, true);
    rel = (rel == UINT32_MAX || rel2 == UINT32_MAX) ? UINT32_MAX : rel + rel2;
    lent = true;
    g.a = g.b = 0;
    heap_caps_walk(NET_HEAP_CAPS, gate_walk_cb, &g);
    *freeh = (uint32_t)heap_caps_get_free_size(NET_HEAP_CAPS);
    r = gate_decide(g.a, g.b, *freeh, fl);
  }
  *largest = g.a;
  *second = g.b;
  aura_log("[net] gate host=%s need=%lu rx=%u margin=%u floor=%lu largest=%lu second=%lu free=%lu ui_cache=%s bands=%s -> %s", host,
           (unsigned long)tls_need_peak(host), (unsigned)TLS_RX_RESERVE, (unsigned)TLS_MARGIN, (unsigned long)fl,
           (unsigned long)g.a, (unsigned long)g.b, (unsigned long)*freeh,
           rel == UINT32_MAX ? "ack-timeout" : (rel ? "released" : "none"), lent ? "lent" : "kept", gate_name(r));
  if (!gate_ok(r)) ui_cache_resume();
  return r;
}

// ------------------------------------------------------------ HTTPS GET ---
// Body bytes are handed to `sink`; it returns false to stop early (enough data).
typedef bool (*BodySink)(const uint8_t *p, size_t n);
static int32_t https_get(const char *host, const char *path, BodySink sink, uint32_t timeout_ms, uint16_t *status,
                         HeapWindow *hw, NetData *n) {
  *status = 0;
  uint32_t largest = 0, second = 0, freeh = 0;
  const GateRes g = tls_gate(host, &largest, &second, &freeh);
  if (!gate_ok(g)) {
    aura_log("[net] skip tls %s ble=%s, next try in %lus", host, ble_is_running() ? "on" : "off",
             (unsigned long)(s_skip_backoff / 1000u));
    hw->largest = largest;
    hw->freeh = freeh;
    return NET_E_HEAP;
  }
  const uint32_t t0 = millis();
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout((NET_TLS_TIMEOUT_MS + 999) / 1000);
  s_tls.setTimeout(NET_TLS_TIMEOUT_MS);
  heap_window_begin(hw);
  if (!s_tls.connect(host, 443, NET_TLS_TIMEOUT_MS)) {
    char err[64];
    err[0] = 0;
    s_tls.lastError(err, sizeof(err));
    s_tls.stop();
    heap_window_end(hw, n);
    aura_log("[net] tls connect %s failed: %s", host, err);
    return NET_E_CONNECT;
  }
  // v3.1.6-wx: the request is written in three bounded pieces (head / path / tail) so the path
  // length is no longer capped by a stack buffer. The 384 B req[] silently failed every Open-Meteo
  // forecast request (path 349 B -> request 463 B -> rl >= sizeof(req) -> NET_E_WRITE, http=0).
  // Each piece is one mbedtls_ssl_write (OUT record 4 KB); no extra heap.
  const size_t pl = path ? strnlen(path, NET_PATH_MAX + 1) : 0;
  char req[160];
  int rl = 0;
  int stage = 0;  // 0 ok, 1 path, 2 head, 3 tail (logged as a number only; the path may carry a key)
  if (pl == 0 || pl > NET_PATH_MAX) stage = 1;
  if (!stage) {
    static const char k_get[] = "GET ";
    if (s_tls.write((const uint8_t *)k_get, sizeof(k_get) - 1) != sizeof(k_get) - 1) stage = 2;
  }
  if (!stage && s_tls.write((const uint8_t *)path, pl) != pl) stage = 1;
  if (!stage) {
    rl = snprintf(req, sizeof(req),
                  " HTTP/1.0\r\nHost: %s\r\nUser-Agent: AURA/" AURA_VERSION
                  "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
                  host);
    if (rl <= 0 || rl >= (int)sizeof(req) || s_tls.write((const uint8_t *)req, (size_t)rl) != (size_t)rl) stage = 3;
  }
  if (stage) {
    wipe(req, sizeof(req));
    s_tls.stop();
    heap_window_end(hw, n);
    aura_log("[net] req write %s failed stage=%d path_len=%u", host, stage, (unsigned)pl);
    return NET_E_WRITE;
  }
  // header: keep the status line, find the blank line with a 4-byte matcher
  char sline[48];
  size_t sl = 0;
  bool in_status = true, in_body = false, stopped = false, timed_out = false;
  uint32_t match = 0, body_bytes = 0;
  bool preempted = false;
  for (;;) {
    if ((uint32_t)(millis() - t0) > timeout_ms) { timed_out = true; break; }
    if (oracle_pending()) { preempted = true; break; }  // Oracle preempts a feed job (one TLS session at a time)
    const int av = s_tls.available();
    if (av <= 0) {
      if (!s_tls.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    const int r = s_tls.read(s_chunk, (size_t)av < sizeof(s_chunk) ? (size_t)av : sizeof(s_chunk));
    if (r <= 0) continue;
    size_t i = 0;
    while (!in_body && i < (size_t)r) {
      const char c = (char)s_chunk[i++];
      if (in_status) {
        if (c == '\r' || c == '\n') { in_status = false; sline[sl] = 0; }
        else if (sl < sizeof(sline) - 1) sline[sl++] = c;
      }
      match = (match << 8) | (uint8_t)c;
      if (match == 0x0D0A0D0Au) {
        in_body = true;
        int code = 0;
        if (http_status_parse(sline, &code)) *status = (uint16_t)code;
        if (code != 200) { stopped = true; break; }
      }
    }
    if (stopped) break;
    if (in_body && i < (size_t)r) {
      body_bytes += (uint32_t)(r - i);
      if (!sink(s_chunk + i, (size_t)r - i)) { stopped = true; break; }
    }
  }
  s_tls.stop();
  heap_window_end(hw, n);
  if (preempted) return NET_E_PREEMPT;
  if (*status == 0) return timed_out ? NET_E_TIMEOUT : NET_E_NOBODY;
  if (*status != 200) return NET_E_STATUS;
  if (body_bytes == 0) return timed_out ? NET_E_TIMEOUT : NET_E_NOBODY;
  if (timed_out && !stopped) return NET_E_TIMEOUT;
  return NET_OK;
}
static bool body_sink(const uint8_t *p, size_t n) {
  const size_t room = sizeof(s_body) - 1 - s_body_n;
  if (n > room) { s_body_over = true; n = room; }
  memcpy(s_body + s_body_n, p, n);
  s_body_n += n;
  s_body[s_body_n] = 0;
  return !s_body_over;
}

// ------------------------------------------------------- JSON scanning ---
// Small, bounded helpers for the known flat Open-Meteo / CoinGecko shapes.
static const char *bfind(const char *s, const char *e, const char *needle) {
  const size_t nl = strlen(needle);
  for (const char *p = s; p && p + nl <= e; p++)
    if (*p == needle[0] && memcmp(p, needle, nl) == 0) return p;
  return nullptr;
}
static bool obj_range(const char *key, const char **s, const char **e) {  // key like "\"current\":{"
  const char *b = strstr(s_body, key);
  if (!b) return false;
  b += strlen(key);
  const char *c = strchr(b, '}');
  if (!c) return false;
  *s = b;
  *e = c;
  return true;
}
static bool num_in(const char *s, const char *e, const char *key, float *out) {
  char k[40];
  snprintf(k, sizeof(k), "\"%s\":", key);
  const char *p = bfind(s, e, k);
  if (!p) return false;
  p += strlen(k);
  while (*p == ' ') p++;
  if (*p == 'n') return false;  // null
  char *end = nullptr;
  const float v = strtof(p, &end);
  if (end == p) return false;
  *out = v;
  return true;
}
// Numeric array; nulls become NAN-less 0 with ok=false. Returns element count.
static int arr_in(const char *s, const char *e, const char *key, float *out, int maxn) {
  char k[48];
  snprintf(k, sizeof(k), "\"%s\":[", key);
  const char *p = bfind(s, e, k);
  if (!p) return 0;
  p += strlen(k);
  int n = 0;
  while (p < e && *p != ']' && n < maxn) {
    while (*p == ' ' || *p == ',') p++;
    if (*p == ']') break;
    if (*p == 'n') { out[n++] = 0; p += 4; continue; }
    char *end = nullptr;
    out[n] = strtof(p, &end);
    if (end == p) break;
    n++;
    p = end;
  }
  return n;
}
// i-th string of an array of ISO times -> minutes after midnight ("...THH:MM")
static int time_min_in(const char *s, const char *e, const char *key, int idx) {
  char k[48];
  snprintf(k, sizeof(k), "\"%s\":[", key);
  const char *p = bfind(s, e, k);
  if (!p) return -1;
  p += strlen(k);
  for (int i = 0; p < e; i++) {
    const char *q = strchr(p, '"');
    if (!q || q >= e) return -1;
    const char *t = strchr(q + 1, 'T');
    const char *qe = strchr(q + 1, '"');
    if (!qe) return -1;
    if (i == idx) {
      if (!t || t > qe || qe - t < 6) return -1;
      return (t[1] - '0') * 600 + (t[2] - '0') * 60 + (t[4] - '0') * 10 + (t[5] - '0');
    }
    p = qe + 1;
  }
  return -1;
}
static int16_t x10(float v) { return (int16_t)(v >= 0 ? v * 10.0f + 0.5f : v * 10.0f - 0.5f); }
// Open-Meteo timezone query. Default IST; net_task copies tz_om() into this
// after the parser slice so the host test still links with the default.
static char s_wx_tz[40] = AURA_TZ_OPENMETEO;

// ----------------------------------------------------------------- jobs ---
static int32_t job_wx(NetData *n, uint16_t *http, HeapWindow *hw) {
  // IoT slim (v3.1.6): drop current apparent_temperature + uv_index (feels->temp, uv<-daily uv_index_max).
  // One TLS; write-path fix is Core's (NET_PATH_MAX / chunked https_get). Path ~319 B with Asia%2FKolkata.
  char path[NET_PATH_MAX + 1];
  const int pn = snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code,is_day"
           "&hourly=temperature_2m&forecast_hours=12"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset,uv_index_max"
           "&forecast_days=5&timezone=%s",
           (double)s_loc.lat, (double)s_loc.lon, s_wx_tz);
  if (pn <= 0 || pn >= (int)sizeof(path)) return NET_E_PARSE;  // never send a truncated query
  s_body_n = 0; s_body_over = false; s_body[0] = 0;
  const int32_t r = https_get(WEATHER_HOST, path, body_sink, NET_TLS_TIMEOUT_MS, http, hw, n);
  if (r != NET_OK) return r;
  if (s_body_over) return NET_E_TOOBIG;
  WxData w;
  memset(&w, 0, sizeof(w));
  const char *s, *e;
  if (!obj_range("\"current\":{", &s, &e)) return NET_E_PARSE;
  float v;
  if (!num_in(s, e, "temperature_2m", &v)) return NET_E_PARSE;
  w.temp_x10 = x10(v);
  w.feels_x10 = num_in(s, e, "apparent_temperature", &v) ? x10(v) : w.temp_x10;
  w.hum = num_in(s, e, "relative_humidity_2m", &v) ? (uint8_t)(v + 0.5f) : 0;
  w.wind_x10 = num_in(s, e, "wind_speed_10m", &v) ? (uint16_t)x10(v) : 0;
  const bool have_uv = num_in(s, e, "uv_index", &v);
  w.uv_x10 = have_uv ? (uint8_t)(v * 10 > 255 ? 255 : x10(v)) : 0;
  w.code = num_in(s, e, "weather_code", &v) ? (uint8_t)v : 0;
  w.is_day = num_in(s, e, "is_day", &v) ? v > 0.5f : true;
  // hourly
  if (obj_range("\"hourly\":{", &s, &e)) {
    float hh[12];
    const int nh = arr_in(s, e, "temperature_2m", hh, 12);
    for (int i = 0; i < nh; i++) w.hour_x10[i] = x10(hh[i]);
    w.nh = (uint8_t)nh;
    const int m = time_min_in(s, e, "time", 0);
    w.hour0 = m >= 0 ? (uint8_t)(m / 60) : 0;
  }
  // daily
  w.sunrise_min = w.sunset_min = -1;
  if (obj_range("\"daily\":{", &s, &e)) {
    float a[5], hi[5], lo[5], pop[5], uv[5];
    const int nd = arr_in(s, e, "weather_code", a, 5);
    const int nhi = arr_in(s, e, "temperature_2m_max", hi, 5);
    const int nlo = arr_in(s, e, "temperature_2m_min", lo, 5);
    const int np = arr_in(s, e, "precipitation_probability_max", pop, 5);
    const int nuv = arr_in(s, e, "uv_index_max", uv, 5);
    int nn = nd < nhi ? nd : nhi;
    if (nlo < nn) nn = nlo;
    for (int i = 0; i < nn; i++) {
      w.day[i].code = (uint8_t)a[i];
      w.day[i].hi_x10 = x10(hi[i]);
      w.day[i].lo_x10 = x10(lo[i]);
      w.day[i].pop = i < np ? (uint8_t)(pop[i] > 100 ? 100 : pop[i]) : 0;
    }
    w.ndays = (uint8_t)nn;
    // P016: slim query has no current uv_index. Daily max stands in by day only; at night UV is 0
    // (the daily max shown at 21:45 read as 'UV 5 MOD'). Clamp like the current path.
    if (!have_uv && nuv > 0) w.uv_x10 = w.is_day ? (uint8_t)(uv[0] * 10 > 255 ? 255 : x10(uv[0])) : 0;
    w.sunrise_min = (int16_t)time_min_in(s, e, "sunrise", 0);
    w.sunset_min = (int16_t)time_min_in(s, e, "sunset", 0);
  }
  w.valid = true;
  ds_set_wx(&w, 50);
  n->temp_valid = true;
  n->temp_x10 = w.temp_x10;
  n->temp_ms = millis();
  return NET_OK;
}
static int32_t job_air(NetData *n, uint16_t *http, HeapWindow *hw) {
  char path[160];
  snprintf(path, sizeof(path), "/v1/air-quality?latitude=%.3f&longitude=%.3f&current=us_aqi,pm2_5,pm10",
           (double)s_loc.lat, (double)s_loc.lon);
  s_body_n = 0; s_body_over = false; s_body[0] = 0;
  const int32_t r = https_get(AIR_HOST, path, body_sink, NET_TLS_TIMEOUT_MS, http, hw, n);
  if (r != NET_OK) return r;
  if (s_body_over) return NET_E_TOOBIG;
  const char *s, *e;
  if (!obj_range("\"current\":{", &s, &e)) return NET_E_PARSE;
  AirData a = {};
  float v;
  if (!num_in(s, e, "us_aqi", &v)) return NET_E_PARSE;
  a.aqi = (uint16_t)(v < 0 ? 0 : v + 0.5f);
  a.pm25_x10 = num_in(s, e, "pm2_5", &v) ? (uint16_t)x10(v) : 0;
  a.pm10_x10 = num_in(s, e, "pm10", &v) ? (uint16_t)x10(v) : 0;
  a.valid = true;
  ds_set_air(&a, 50);
  return NET_OK;
}
static const char *const k_cg_id[4] = {"bitcoin", "ethereum", "solana", "ripple"};
static const char *const k_cg_sym[4] = {"BTC", "ETH", "SOL", "XRP"};
static const char *const k_cg_name[4] = {"Bitcoin", "Ethereum", "Solana", "XRP"};
// v3.1.6 p0p1: one GET of /coins/markets with the 7-day hourly sparkline (~16 KB for 4 coins, ~3 KB
// more than nothing compared with simple/price + a history call per coin). Streamed through CgScan:
// the body is never stored, only price/24 h change/volume/hi/lo and the last CRYPTO_SPARK_N points.
static CgScan s_cg;
static bool cg_sink(const uint8_t *p, size_t n) { return cg_scan_feed(&s_cg, p, n); }
static int32_t job_crypto(NetData *n, uint16_t *http, HeapWindow *hw) {
  cg_scan_init(&s_cg, k_cg_id, 4);
  const int32_t r = https_get(CRYPTO_HOST,
                              "/api/v3/coins/markets?vs_currency=usd&ids=bitcoin,ethereum,solana,ripple&sparkline=true",
                              cg_sink, NET_CRYPTO_TIMEOUT_MS, http, hw, n);
  CryptoData c = {};
  c.n = cg_scan_take(&s_cg, c.c, 4);
  if (r != NET_OK && !(r == NET_E_TIMEOUT && c.n == 4)) return r;  // a late close after all 4 coins still counts
  if (c.n == 0) return NET_E_PARSE;
  for (uint8_t i = 0, k = 0; k < 4 && i < c.n; k++) {  // cg_scan_take keeps id-table order and skips missing ids
    if (!s_cg.got[k]) continue;
    snprintf(c.c[i].sym, sizeof(c.c[i].sym), "%s", k_cg_sym[k]);
    snprintf(c.c[i].name, sizeof(c.c[i].name), "%s", k_cg_name[k]);
    i++;
  }
  c.valid = true;
  ds_set_crypto(&c, 50);
  return NET_OK;
}

// ---- RSS stream parser (bounded: 16 B tag name, 192 B raw title) ---------
enum RssSt : uint8_t { RS_TEXT = 0, RS_TAG, RS_CDATA };
static struct {
  uint8_t st;
  char tag[17];
  uint8_t tl;
  bool in_item, capture, have_src;
  char raw[192];
  uint16_t rl;
  uint8_t cd;  // count of trailing ']' while in CDATA
  NewsData out;
} s_rss;
static void rss_reset(void) { memset(&s_rss, 0, sizeof(s_rss)); }
// decode entities + common UTF-8 punctuation into the 0x20..0x7E font range
static void rss_commit(char *dst, size_t cap) {
  const char *p = s_rss.raw;
  const char *e = s_rss.raw + s_rss.rl;
  size_t o = 0;
  bool sp = true;
  auto put = [&](char c) {
    if (c == ' ') { if (sp) return; sp = true; } else sp = false;
    if (o + 1 < cap) dst[o++] = c;
  };
  while (p < e) {
    const uint8_t c = (uint8_t)*p;
    if (c == '&') {
      const char *semi = (const char *)memchr(p, ';', (size_t)(e - p) < 8 ? (size_t)(e - p) : 8);
      if (semi) {
        const size_t l = (size_t)(semi - p - 1);
        const char *nm = p + 1;
        char ch = 0;
        if (l == 3 && !memcmp(nm, "amp", 3)) ch = '&';
        else if (l == 2 && !memcmp(nm, "lt", 2)) ch = '<';
        else if (l == 2 && !memcmp(nm, "gt", 2)) ch = '>';
        else if (l == 4 && !memcmp(nm, "quot", 4)) ch = '"';
        else if (l == 4 && !memcmp(nm, "apos", 4)) ch = '\'';
        else if (l >= 2 && nm[0] == '#') {
          const long cp = nm[1] == 'x' ? strtol(nm + 2, nullptr, 16) : strtol(nm + 1, nullptr, 10);
          ch = cp == 8216 || cp == 8217 || cp == 39 ? '\'' : cp == 8220 || cp == 8221 || cp == 34 ? '"' :
               cp == 8211 || cp == 8212 ? '-' : (cp >= 32 && cp < 127) ? (char)cp : ' ';
        }
        if (ch) { put(ch); p = semi + 1; continue; }
      }
      put('&');
      p++;
      continue;
    }
    if (c < 0x80) { put(c < 0x20 ? ' ' : (char)c); p++; continue; }
    // UTF-8: E2 80 98/99 quotes, 9C/9D dquotes, 93/94 dashes, A6 ellipsis; C2 A0 nbsp
    if (c == 0xE2 && e - p >= 3 && (uint8_t)p[1] == 0x80) {
      const uint8_t c3 = (uint8_t)p[2];
      put(c3 == 0x98 || c3 == 0x99 ? '\'' : c3 == 0x9C || c3 == 0x9D ? '"' : c3 == 0x93 || c3 == 0x94 ? '-' :
          c3 == 0xA6 ? (char)0x81 : ' ');
      p += 3;
      continue;
    }
    if (c == 0xC2 && e - p >= 2) { put(' '); p += 2; continue; }
    if (c == 0xC3 && e - p >= 2) {  // Latin-1 letters -> base letter (font is ASCII only)
      static const char fold[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
      const uint8_t c2 = (uint8_t)p[1];
      if (c2 >= 0x80 && c2 <= 0xBF) put(fold[c2 - 0x80]);
      p += 2;
      continue;
    }
    p++;
    while (p < e && ((uint8_t)*p & 0xC0) == 0x80) p++;  // skip other code points
  }
  while (o && dst[o - 1] == ' ') o--;
  dst[o] = 0;
}
static void rss_raw_put(char c) {
  if (s_rss.capture && s_rss.rl < sizeof(s_rss.raw)) s_rss.raw[s_rss.rl++] = c;
}
static void rss_tag_done(void) {
  s_rss.tag[s_rss.tl] = 0;
  const char *t = s_rss.tag;
  if (!strcmp(t, "item")) s_rss.in_item = true;
  else if (!strcmp(t, "/item")) s_rss.in_item = false;
  else if (!strcmp(t, "title")) {
    if (s_rss.in_item || !s_rss.have_src) { s_rss.capture = true; s_rss.rl = 0; }
  } else if (!strcmp(t, "/title") && s_rss.capture) {
    s_rss.capture = false;
    if (s_rss.in_item) {
      if (s_rss.out.n < NEWS_MAX) {
        rss_commit(s_rss.out.title[s_rss.out.n], NEWS_TITLE_MAX);
        if (s_rss.out.title[s_rss.out.n][0]) s_rss.out.n++;
      }
    } else {
      char src[48];
      rss_commit(src, sizeof(src));
      size_t i = 0;
      for (; src[i] && i < sizeof(s_rss.out.src) - 1; i++) s_rss.out.src[i] = (char)(src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i]);
      s_rss.out.src[i] = 0;
      s_rss.have_src = true;
    }
  }
}
static bool rss_sink(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const char c = (char)p[i];
    switch (s_rss.st) {
      case RS_TEXT:
        if (c == '<') { s_rss.st = RS_TAG; s_rss.tl = 0; }
        else rss_raw_put(c);
        break;
      case RS_TAG:
        if (c == '>') { rss_tag_done(); s_rss.st = RS_TEXT; break; }
        if (s_rss.tl < 16) s_rss.tag[s_rss.tl++] = c;
        if (s_rss.tl == 8 && !memcmp(s_rss.tag, "![CDATA[", 8)) { s_rss.st = RS_CDATA; s_rss.cd = 0; break; }
        if (c == ' ' && s_rss.tl) s_rss.tl--;  // attributes: keep the name only
        if (c == ' ') { s_rss.tag[s_rss.tl] = 0; s_rss.tl = 16; }  // ignore the rest until '>'
        break;
      case RS_CDATA:
        if (c == '>' && s_rss.cd >= 2) {  // "]]>": drop the two ']' already buffered
          if (s_rss.capture && s_rss.rl >= 2) s_rss.rl -= 2;
          s_rss.st = RS_TEXT;
          break;
        }
        s_rss.cd = c == ']' ? (uint8_t)(s_rss.cd + 1) : 0;
        rss_raw_put(c);
        break;
    }
    if (s_rss.out.n >= NEWS_MAX) return false;  // enough: close the connection
  }
  return true;
}
static int32_t job_news(NetData *n, uint16_t *http, HeapWindow *hw) {
  rss_reset();
  const int32_t r = https_get(NEWS_HOST, NEWS_PATH, rss_sink, NET_NEWS_TIMEOUT_MS, http, hw, n);
  if (s_rss.out.n == 0) return r != NET_OK ? r : NET_E_PARSE;
  if (!s_rss.out.src[0]) snprintf(s_rss.out.src, sizeof(s_rss.out.src), "BBC NEWS");
  s_rss.out.valid = true;
  ds_set_news(&s_rss.out, 50);
  return NET_OK;  // partial (timed out after >= 1 title) still counts as fresh data
}

static WebServer *s_web = nullptr;
static DNSServer *s_dns = nullptr;
// ---------------------------------------------------------------- oracle ---
// xAI or Groq chat completions (both OpenAI-compatible: same body, Bearer header,
// SSE and one-shot paths; Groq gets no reasoning_effort). One TLS session at a time: the
// request runs between feed jobs and preempts one that is reading. SSE over
// HTTP/1.1 (chunked) when the heap gate passes with ORACLE_SSE_MARGIN to spare,
// else a one-shot HTTP/1.0 request whose JSON is scanned for message.content.
// The key is read (NVS, else the secrets.h default) into a stack buffer only for the
// request header and wiped right after the write; only its length is logged.
static OracleData s_or;
static OrStream s_ors;
// Request buffers for job_oracle (v3.1.1 fix1: static instead of ~2 KB of net stack).
// Owned exclusively by the net task: job_oracle is only ever called from net_task()'s
// loop, so there is a single user and no other task, callback or ISR touches them; no
// lock needed. They hold the escaped prompt/body only during one request and are wiped
// (volatile loop) on every exit path of job_oracle. The key never goes into them.
static char s_or_esc[ORACLE_PROMPT_MAX * 2];
static char s_or_body[1280];
static volatile bool s_or_cfg_dirty = true;
static const char k_or_system[] =
    "You are Oracle, the assistant on AURA, a small desk display. Answer in plain text (no markdown, no emoji, no "
    "lists), at most 3 short sentences, under 500 characters.";

static void wipe(void *p, size_t n) {
  volatile uint8_t *v = (volatile uint8_t *)p;
  while (n--) *v++ = 0;
}
static void or_publish(void) { ds_set_oracle(&s_or, 20); }
// Per-API settings (NVS "oracle": "model" = xAI model, "gmodel" = Groq model (v3.1.2),
// "mmodel" = Gemini model (v3.1.6 p0p1), "prov" = 0 device / 1 PC app, "dprov" = device API selection ORA_SEL_*).
static char s_xai_model[32], s_groq_model[32], s_gem_model[32];
struct OrApi { const char *host, *path, *name, *err_host; };
// Index = ORA_XAI / ORA_GROQ / ORA_GEMINI. Gemini's path is built per request (model in the path).
static const OrApi k_or_api[3] = {{ORACLE_HOST, ORACLE_PATH, "xai", "CAN'T REACH API.X.AI"},
                                  {GROQ_HOST, GROQ_PATH, "groq", "CAN'T REACH API.GROQ.COM"},
                                  {GEMINI_HOST, GEMINI_PATH_FMT, "gemini", "CAN'T REACH GOOGLEAPIS"}};
static int or_key_index(uint8_t api) { return api == ORA_GEMINI ? KI_GEMINI : api == ORA_GROQ ? KI_GROQ : KI_XAI; }
static bool or_key_present(int ki) {  // NVS key or built-in default; never reads the value
  if (k_key_dflt[ki][0]) return true;
  Preferences p;
  bool have = false;
  if (p.begin("keys", true)) { have = p.isKey(k_key_names[ki]); p.end(); }
  return have;
}
// Provider resolution: a manual selection (portal "dprov") wins; otherwise Gemini if a Gemini key
// is set (v3.1.6), else xAI if an xAI key is set (NVS or XAI_API_KEY_DEFAULT), else Groq.
static void or_resolve(void) {
  const bool hx = or_key_present(KI_XAI), hg = or_key_present(KI_GROQ), hm = or_key_present(KI_GEMINI);
  s_or.api = ora_resolve(s_or.api_sel, hx, hm);
  s_or.key_ok = s_or.api == ORA_XAI ? hx : s_or.api == ORA_GEMINI ? hm : hg;
  snprintf(s_or.model, sizeof(s_or.model), "%s",
           s_or.api == ORA_XAI ? s_xai_model : s_or.api == ORA_GEMINI ? s_gem_model : s_groq_model);
}
static void or_cfg_load(void) {
  snprintf(s_xai_model, sizeof(s_xai_model), "%s", ORACLE_MODEL_DEFAULT);
  snprintf(s_groq_model, sizeof(s_groq_model), "%s", GROQ_MODEL_DEFAULT);
  snprintf(s_gem_model, sizeof(s_gem_model), "%s", GEMINI_MODEL_DEFAULT);
  s_or.provider = 0;
  s_or.api_sel = ORA_SEL_AUTO;
  Preferences p;
  if (p.begin("oracle", true)) {  // v3.1.1 namespace: new keys only
    if (p.isKey("model")) p.getString("model", s_xai_model, sizeof(s_xai_model));
    if (p.isKey("gmodel")) p.getString("gmodel", s_groq_model, sizeof(s_groq_model));
    if (p.isKey("mmodel")) p.getString("mmodel", s_gem_model, sizeof(s_gem_model));
    s_or.provider = p.getUChar("prov", 0) ? 1 : 0;
    const uint8_t ds = p.getUChar("dprov", ORA_SEL_AUTO);
    s_or.api_sel = ds <= ORA_SEL_GEMINI ? ds : (uint8_t)ORA_SEL_AUTO;
    p.end();
  }
  if (!s_xai_model[0]) snprintf(s_xai_model, sizeof(s_xai_model), "%s", ORACLE_MODEL_DEFAULT);
  if (!s_groq_model[0]) snprintf(s_groq_model, sizeof(s_groq_model), "%s", GROQ_MODEL_DEFAULT);
  if (!s_gem_model[0]) snprintf(s_gem_model, sizeof(s_gem_model), "%s", GEMINI_MODEL_DEFAULT);
  or_resolve();
  or_publish();
  static const char *const sel[4] = {"auto", "xai", "groq", "gemini"};
  aura_log("[oracle] provider=%s api=%s (select=%s) model=%s key=%s", s_or.provider ? "pc" : "device",
           k_or_api[s_or.api].name, sel[s_or.api_sel], s_or.model, s_or.key_ok ? "set" : "missing");
}
// model / gmodel / mmodel: nullptr = keep, "" = default. prov, dprov: < 0 = keep.
static bool or_cfg_save(const char *model, const char *gmodel, const char *mmodel, int prov, int dprov) {
  Preferences p;
  if (!p.begin("oracle", false)) return false;
  bool ok = true;
  if (model) {
    if (model[0]) ok = p.putString("model", model) == strlen(model);
    else { p.remove("model"); }
  }
  if (gmodel) {
    if (gmodel[0]) ok = ok && p.putString("gmodel", gmodel) == strlen(gmodel);
    else { p.remove("gmodel"); }
  }
  if (mmodel) {
    if (mmodel[0]) ok = ok && p.putString("mmodel", mmodel) == strlen(mmodel);
    else { p.remove("mmodel"); }
  }
  if (prov >= 0) ok = ok && p.putUChar("prov", (uint8_t)(prov ? 1 : 0)) == 1;
  if (dprov >= 0) ok = ok && p.putUChar("dprov", (uint8_t)(dprov <= ORA_SEL_GEMINI ? dprov : ORA_SEL_AUTO)) == 1;
  p.end();
  s_or_cfg_dirty = true;
  return ok;
}
static void or_end(uint8_t st, const char *reason) {
  s_or.st = st;
  snprintf(s_or.reason, sizeof(s_or.reason), "%s", reason ? reason : "");
  or_publish();
}
static void or_take_text(void) {
  const uint16_t n = s_ors.r.n < ORACLE_TEXT_MAX - 1 ? s_ors.r.n : (uint16_t)(ORACLE_TEXT_MAX - 1);
  memcpy(s_or.text, s_ors.r.s, n);
  s_or.text[n] = 0;
  s_or.n = n;
}
static void job_oracle(NetData *n, bool net_up) {
  char prompt[ORACLE_PROMPT_MAX];
  // wipes the prompt copy and the static request buffers on every return path
  struct OrWipe {
    char *p;
    ~OrWipe() { wipe(p, ORACLE_PROMPT_MAX); wipe(s_or_esc, sizeof(s_or_esc)); wipe(s_or_body, sizeof(s_or_body)); }
  } or_wipe_guard{prompt};
  taskENTER_CRITICAL(&s_or_mux);
  memcpy(prompt, s_or_prompt, sizeof(prompt));
  const uint32_t seq = s_or_req_seq;
  taskEXIT_CRITICAL(&s_or_mux);
  s_or_done_seq = seq;
  s_or.seq = seq;
  s_or.n = 0;
  s_or.text[0] = 0;
  s_or.http = 0;
  s_or.sse = false;
  s_or.retry_until_ms = 0;
  s_or.reason[0] = 0;
  s_or.st = OR_THINKING;
  or_publish();
  if (s_or.provider) { or_end(OR_ERROR, "PROVIDER IS PC APP"); return; }
  if (!net_up) { or_end(OR_ERROR, s_web ? "SETUP PORTAL ACTIVE" : "OFFLINE"); return; }
  if (!json_escape(prompt, s_or_esc, sizeof(s_or_esc))) { or_end(OR_ERROR, "PROMPT TOO LONG"); return; }
  or_resolve();  // a key may have been added or cleared since the last publish
  const OrApi &api = k_or_api[s_or.api];
  char key[KEY_MAX + 1];
  uint8_t ksrc = 0;
  const bool gem = s_or.api == ORA_GEMINI;
  const size_t kl = key_load(or_key_index(s_or.api), key, &ksrc);
  if (!kl) { wipe(key, sizeof(key)); or_end(OR_ERROR, "NO KEY - ADD IT IN SETUP"); return; }
  for (size_t i = 0; i < kl; i++)  // the key goes into a header line: printable ASCII only (no CR/LF injection)
    if (key[i] <= ' ' || key[i] > '~') { wipe(key, sizeof(key)); or_end(OR_KEYBAD, "KEY HAS BAD CHARACTERS"); return; }
  uint32_t lg = 0, sec = 0, fr = 0, fl = 0;
  const GateRes g = tls_gate(api.host, &lg, &sec, &fr, &fl);
  if (!gate_ok(g)) {
    wipe(key, sizeof(key));
    n->tls_skipped++;
    aura_log("[oracle] seq=%lu api=%s gate=%s free=%lu floor=%lu largest=%lu second=%lu", (unsigned long)seq, api.name,
             gate_name(g), (unsigned long)fr, (unsigned long)fl, (unsigned long)lg, (unsigned long)sec);
    or_end(OR_ERROR, "RAM TIGHT - PRESS TO RETRY");
    return;
  }
  // Gemini: one-shot only (generateContent; no SSE parser for streamGenerateContent).
  const bool sse = !gem && g == GATE_OK_SINGLE && lg >= GATE_SINGLE_MIN + ORACLE_SSE_MARGIN && fr >= fl + ORACLE_SSE_MARGIN;
  s_or.sse = sse;
  // grok-3-mini is a reasoning model: keep its reasoning short so max_tokens leaves room for the answer.
  // Groq: never send reasoning_effort.
  const bool low = s_or.api == ORA_XAI && strncmp(s_or.model, "grok-3-mini", 11) == 0;
  char model_esc[48];
  json_escape(s_or.model, model_esc, sizeof(model_esc));
  const int bl = gem ? gm_build_body(s_or_body, sizeof(s_or_body), s_or.model, k_or_system, s_or_esc, ORACLE_MAX_TOKENS,
                                     ORACLE_TEMPERATURE)
                     : or_build_body(s_or_body, sizeof(s_or_body), model_esc, k_or_system, s_or_esc, ORACLE_MAX_TOKENS,
                                     ORACLE_TEMPERATURE, sse, low);
  if (bl <= 0 || bl >= (int)sizeof(s_or_body)) { wipe(key, sizeof(key)); or_end(OR_ERROR, "PROMPT TOO LONG"); return; }
  aura_log("[oracle] seq=%lu api=%s model=%s mode=%s prompt_len=%u body_len=%d key_len=%u key_src=%s", (unsigned long)seq,
           api.name, s_or.model, sse ? "sse/http1.1" : "oneshot/http1.0", (unsigned)strlen(prompt), bl, (unsigned)kl,
           ksrc == 2 ? "default" : "nvs");
  HeapWindow hw = {0, UINT32_MAX, UINT32_MAX};
  const uint32_t t0 = millis();
  s_tls.useBuiltinCACertBundle();
  s_tls.setHandshakeTimeout((NET_TLS_TIMEOUT_MS + 999) / 1000);
  s_tls.setTimeout(NET_TLS_TIMEOUT_MS);
  heap_window_begin(&hw);
  if (!s_tls.connect(api.host, 443, NET_TLS_TIMEOUT_MS)) {
    wipe(key, sizeof(key));
    s_tls.stop();
    heap_window_end(&hw, n);
    aura_log("[oracle] tls connect failed api=%s took=%lums heap_before=%lu heap_min=%lu largest_min=%lu", api.name,
             (unsigned long)(millis() - t0), (unsigned long)fr, (unsigned long)hw.freeh, (unsigned long)hw.largest);
    or_end(OR_ERROR, api.err_host);
    return;
  }
  const uint32_t t_hs = millis() - t0;
  // v3.1.6 p0p1: +88 B for Gemini (model in the path, "x-goog-api-key" header; the key is never in the URL).
  char hdr[288 + KEY_MAX];
  char gpath[80];
  if (gem) snprintf(gpath, sizeof(gpath), GEMINI_PATH_FMT, s_or.model);  // model: portal charset [A-Za-z0-9._-]
  const int hl = snprintf(hdr, sizeof(hdr),
                          "POST %s HTTP/1.%c\r\nHost: %s\r\n%s%s\r\n"
                          "Content-Type: application/json\r\nAccept: %s\r\nUser-Agent: AURA/" AURA_VERSION
                          "\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                          gem ? gpath : api.path, sse ? '1' : '0', api.host, gem ? "x-goog-api-key: " : "Authorization: Bearer ",
                          key, sse ? "text/event-stream" : "application/json", bl);
  wipe(key, sizeof(key));
  const bool wr = hl > 0 && hl < (int)sizeof(hdr) && s_tls.write((const uint8_t *)hdr, (size_t)hl) == (size_t)hl &&
                  s_tls.write((const uint8_t *)s_or_body, (size_t)bl) == (size_t)bl;
  wipe(hdr, sizeof(hdr));
  if (!wr) {
    s_tls.stop();
    heap_window_end(&hw, n);
    or_end(OR_ERROR, "SEND FAILED");
    return;
  }
  ors_init(&s_ors, sse, gem);
  uint32_t deadline = millis() + ORACLE_TIMEOUT_MS, last_pub = 0;
  bool timed_out = false;
  for (;;) {
    const uint32_t now = millis();
    if ((int32_t)(now - deadline) > 0) { timed_out = true; break; }
    const int av = s_tls.available();
    if (av <= 0) {
      if (!s_tls.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    const int r = s_tls.read(s_chunk, (size_t)av < sizeof(s_chunk) ? (size_t)av : sizeof(s_chunk));
    if (r <= 0) continue;
    const uint16_t before = s_ors.r.n;
    const bool stop = ors_feed(&s_ors, s_chunk, (size_t)r);
    if (s_ors.r.n != before) {
      deadline = now + ORACLE_TIMEOUT_MS;  // tokens flowing: the timeout is the gap between them
      if (now - last_pub >= 120u) {
        last_pub = now;
        or_take_text();
        s_or.st = OR_STREAM;
        or_publish();
      }
    }
    if (stop) break;
  }
  s_tls.stop();
  heap_window_end(&hw, n);
  reply_finish(&s_ors.r);
  or_take_text();
  const uint16_t st = s_ors.h.status;
  s_or.http = st;
  const uint32_t took = millis() - t0;
  aura_log("[oracle] seq=%lu api=%s http=%u mode=%s handshake=%lums took=%lums reply_len=%u capped=%d timeout=%d "
           "heap_before=%lu heap_min=%lu heap_after=%lu largest_min=%lu",
           (unsigned long)seq, api.name, (unsigned)st, sse ? "sse" : "oneshot", (unsigned long)t_hs, (unsigned long)took,
           (unsigned)s_or.n, s_ors.r.full ? 1 : 0, timed_out ? 1 : 0, (unsigned long)fr, (unsigned long)hw.freeh,
           (unsigned long)heap_caps_get_free_size(NET_HEAP_CAPS), (unsigned long)hw.largest);
  if (st == 200) {
    if (s_or.n) { n->fetch_ok++; or_end(OR_DONE, timed_out && !ors_complete(&s_ors) ? "CUT OFF (TIMEOUT)" : ""); }
    else or_end(OR_ERROR, timed_out ? "NO REPLY IN 20 S" : "EMPTY REPLY");
    return;
  }
  n->fetch_fail++;
  s_or.n = 0;
  s_or.text[0] = 0;
  char b[32];
  if (st == 429) {
    const uint32_t ra = s_ors.h.retry_after_s > 0 ? (uint32_t)s_ors.h.retry_after_s : ORACLE_BUSY_DEFAULT_S;
    s_or.retry_until_ms = millis() + (ra > 600u ? 600u : ra) * 1000u;
    or_end(OR_BUSY, "RATE LIMITED");
  } else if (st == 401 || st == 403) {
    or_end(OR_KEYBAD, st == 401 ? "KEY REJECTED (401)" : "KEY NOT ALLOWED (403)");
  } else if (gem && s_ors.key_invalid) {  // Gemini answers 400 + reason API_KEY_INVALID to a bad key
    or_end(OR_KEYBAD, "KEY REJECTED (400)");
  } else if (st == 0) {
    or_end(OR_ERROR, timed_out ? "NO REPLY IN 20 S" : "NO RESPONSE");
  } else {
    snprintf(b, sizeof(b), "HTTP %u%s", (unsigned)st, st == 400 ? " BAD REQUEST" : st == 404 ? " UNKNOWN MODEL" : st >= 500 ? " SERVER ERROR" : "");
    or_end(OR_ERROR, b);
  }
}

// Keyed feeds. Placed after the nettest slice (which ends at s_web) so the host
// extractor does not need ds_set_stocks / ds_set_cricket. Paths contain the key:
// copy it only into `path`, wipe the key and the path, and never log either.
static bool key_url_safe(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    const unsigned char c = *p;
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                    c == '_' || c == '~';
    if (!ok) return false;
  }
  return true;
}
// v3.1.6 p0p1: the provider follows the key. Twelve Data keys are 32 hex chars (batch /quote, one call);
// anything else is treated as an Alpha Vantage key (the shipped STOCK default is one: Twelve Data and
// Finnhub answer 401 to it). AV free = 25 calls/day, one symbol per GLOBAL_QUOTE: one symbol per run,
// missing rows first (boot fill ~75 s apart), then every NET_PERIOD_STOCK_AV_MS. Rows persist here.
static const char *const k_stk_sym[STOCK_N] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
static const char *const k_stk_name[STOCK_N] = {"Apple", "Microsoft", "Alphabet", "Amazon"};
static StockRow s_av_row[STOCK_N];
static bool s_av_have[STOCK_N];
static uint8_t s_av_next = 0;
static uint32_t s_job_period_ms = 0;  // set by a job to override its k_jobs[] period after a success (0 = default)
static int32_t job_stocks_av(const char *key, NetData *n, uint16_t *http, HeapWindow *hw) {
  uint8_t i = s_av_next % STOCK_N;
  for (uint8_t k = 0; k < STOCK_N; k++)  // fill gaps first
    if (!s_av_have[(s_av_next + k) % STOCK_N]) { i = (uint8_t)((s_av_next + k) % STOCK_N); break; }
  char path[160];
  const int pl = snprintf(path, sizeof(path), "/query?function=GLOBAL_QUOTE&symbol=%s&apikey=%s", k_stk_sym[i], key);
  if (pl <= 0 || pl >= (int)sizeof(path)) { wipe(path, sizeof(path)); return NET_E_PARSE; }
  s_body_n = 0; s_body_over = false; s_body[0] = 0;
  const int32_t r = https_get(STOCK_AV_HOST, path, body_sink, NET_TLS_TIMEOUT_MS, http, hw, n);
  wipe(path, sizeof(path));
  if (r != NET_OK) { wipe(s_body, sizeof(s_body)); return r; }
  float price = 0, chg = 0;
  const uint8_t q = s_body_over ? (uint8_t)AVQ_PARSE : av_quote_parse(s_body, &price, &chg);
  wipe(s_body, sizeof(s_body));
  s_body_n = 0;
  if (q == AVQ_KEY) { *http = 401; return NET_E_KEY; }
  if (q == AVQ_RATE) { *http = 429; return NET_E_RATE; }
  if (q != AVQ_OK) return NET_E_PARSE;
  StockRow &w = s_av_row[i];
  snprintf(w.sym, sizeof(w.sym), "%s", k_stk_sym[i]);
  snprintf(w.name, sizeof(w.name), "%s", k_stk_name[i]);
  w.price = price;
  w.chg = chg;
  s_av_have[i] = true;
  s_av_next = (uint8_t)((i + 1) % STOCK_N);
  StocksData st = {};
  bool all = true;
  for (uint8_t k = 0; k < STOCK_N; k++) {
    if (s_av_have[k]) st.row[st.n++] = s_av_row[k];
    else all = false;
  }
  st.valid = st.n > 0;
  ds_set_stocks(&st, 50);
  s_job_period_ms = all ? NET_PERIOD_STOCK_AV_MS : NET_PERIOD_STOCK_FILL_MS;
  return NET_OK;
}
static int32_t job_stocks(NetData *n, uint16_t *http, HeapWindow *hw) {
  char key[KEY_MAX + 1];
  const size_t kl = key_load(KI_STOCKS, key, nullptr);
  if (!kl || !key_url_safe(key)) { wipe(key, sizeof(key)); return NET_E_PARSE; }
  if (kl != 32) {
    const int32_t r = job_stocks_av(key, n, http, hw);
    wipe(key, sizeof(key));
    return r;
  }
  char path[192];
  const int pl = snprintf(path, sizeof(path), "/quote?symbol=AAPL,MSFT,GOOGL,AMZN&apikey=%s", key);
  wipe(key, sizeof(key));
  if (pl <= 0 || pl >= (int)sizeof(path)) { wipe(path, sizeof(path)); return NET_E_PARSE; }
  s_body_n = 0; s_body_over = false; s_body[0] = 0;
  const int32_t r = https_get(STOCK_HOST, path, body_sink, NET_TLS_TIMEOUT_MS, http, hw, n);
  wipe(path, sizeof(path));
  if (r != NET_OK) return r;
  if (s_body_over) { wipe(s_body, sizeof(s_body)); return NET_E_TOOBIG; }
  StocksData st;
  const bool ok = stock_parse(s_body, &st);
  // Twelve Data reports a bad key / quota as 200 + {"code":401|429,...} on some plans
  const bool key_bad = !ok && strstr(s_body, "\"code\":401"), rate = !ok && strstr(s_body, "\"code\":429");
  wipe(s_body, sizeof(s_body));
  s_body_n = 0;
  if (key_bad) { *http = 401; return NET_E_KEY; }
  if (rate) { *http = 429; return NET_E_RATE; }
  if (!ok) return NET_E_PARSE;
  ds_set_stocks(&st, 50);
  return NET_OK;
}
static CricScan s_cric;
static bool cric_sink(const uint8_t *p, size_t n) { return cric_scan_feed(&s_cric, p, n); }
static int32_t job_cricket(NetData *n, uint16_t *http, HeapWindow *hw) {
  char key[KEY_MAX + 1];
  const size_t kl = key_load(KI_CRICKET, key, nullptr);
  if (!kl || !key_url_safe(key)) { wipe(key, sizeof(key)); return NET_E_PARSE; }
  char path[192];
  const int pl = snprintf(path, sizeof(path), "/v1/currentMatches?apikey=%s&offset=0", key);
  wipe(key, sizeof(key));
  if (pl <= 0 || pl >= (int)sizeof(path)) { wipe(path, sizeof(path)); return NET_E_PARSE; }
  cric_scan_init(&s_cric);
  const int32_t r = https_get(CRIC_HOST, path, cric_sink, NET_TLS_TIMEOUT_MS, http, hw, n);
  wipe(path, sizeof(path));
  CricketData cd;
  cric_scan_take(&s_cric, &cd);
  cric_scan_init(&s_cric);  // drop match text; the body was never stored (it echoes the key)
  if (r != NET_OK && r != NET_E_TIMEOUT) return r;
  if (!cd.n) return r != NET_OK ? r : NET_E_PARSE;
  ds_set_cricket(&cd, 50);
  return NET_OK;  // a short read after a few matches still counts
}
static bool job_armed(int j) {
  if (j == JOB_STOCKS) return s_have_stocks;
  if (j == JOB_CRICKET) return s_have_cricket;
  return true;
}

// ------------------------------------------------------------ scheduler ---
typedef int32_t (*JobFn)(NetData *, uint16_t *, HeapWindow *);
static const struct { const char *name; JobFn fn; uint32_t period; } k_jobs[JOB_COUNT] = {
    {"wx", job_wx, NET_PERIOD_WX_MS},
    {"air", job_air, NET_PERIOD_AIR_MS},
    {"crypto", job_crypto, NET_PERIOD_CRYPTO_MS},
    {"news", job_news, NET_PERIOD_NEWS_MS},
    {"stocks", job_stocks, NET_PERIOD_STOCK_MS},
    {"cricket", job_cricket, NET_PERIOD_CRIC_MS},
};
static const uint32_t k_retry_ms[NET_JOB_RETRIES] = {20000u, 60000u, 180000u};
static JobInfo s_job[JOB_COUNT];

static void run_job(int j, NetData *n, uint32_t now) {
  JobInfo &ji = s_job[j];
  const uint8_t prev = ji.st;
  ji.st = JS_LOADING;
  ds_set_job((uint8_t)j, &ji, 10);
  uint16_t http = 0;
  HeapWindow hw = {0, UINT32_MAX, UINT32_MAX};
  const uint32_t t0 = millis();
  s_job_period_ms = 0;
  const int32_t r = k_jobs[j].fn(n, &http, &hw);
  ji.http = http;
  ji.err = (int16_t)r;
  n->http_status = http;
  n->last_err = r;
  const uint32_t t1 = millis();
  if (r == NET_OK) {
    ji.st = JS_OK;
    ji.attempt = 0;
    ji.ok_ms = t1;
    ji.next_ms = now + (s_job_period_ms ? s_job_period_ms : k_jobs[j].period);
    n->fetch_ok++;
    s_skip_backoff = NET_TLS_SKIP_BACKOFF_MIN_MS;
  } else if (r == NET_E_PREEMPT) {
    ji.st = prev == JS_LOADING ? (uint8_t)JS_NEVER : prev;  // not a failure: rerun right after the Oracle request
    ji.next_ms = now + 1500u;
  } else if (r == NET_E_HEAP) {
    ji.st = JS_HEAP;
    ji.next_ms = now + s_skip_backoff;
    n->fetch_fail++;
    n->tls_skipped++;
    s_skip_backoff = s_skip_backoff * 2 > NET_TLS_SKIP_BACKOFF_MAX_MS ? NET_TLS_SKIP_BACKOFF_MAX_MS : s_skip_backoff * 2;
  } else if (r == NET_E_KEY || r == NET_E_RATE || (r == NET_E_STATUS && (http == 401 || http == 403 || http == 429))) {
    // v3.1.6 p0p1: a rejected key or a spent quota does not heal in 20/60/180 s: show it, wait a full period
    // (stocks waits at least NET_PERIOD_STOCK_AV_MS so an Alpha Vantage quota error cannot burn the day's calls).
    n->fetch_fail++;
    ji.st = JS_FAIL;
    ji.attempt = 0;
    ji.next_ms = now + (j == JOB_STOCKS && k_jobs[j].period < NET_PERIOD_STOCK_AV_MS ? NET_PERIOD_STOCK_AV_MS : k_jobs[j].period);
  } else {
    n->fetch_fail++;
    if (prev != JS_RETRY) ji.attempt = 0;
    if (ji.attempt < NET_JOB_RETRIES) {
      ji.next_ms = now + k_retry_ms[ji.attempt];
      ji.attempt++;
      ji.st = JS_RETRY;
    } else {
      ji.st = JS_FAIL;
      ji.attempt = 0;
      ji.next_ms = now + k_jobs[j].period;
    }
  }
  ui_cache_resume();  // idempotent safety net (every TLS path also resumes)
  ds_set_job((uint8_t)j, &ji, 10);
  aura_log("[net] job=%s r=%ld http=%u st=%u took=%lums sess_largest_min=%lu sess_free_min=%lu tls_largest_min=%lu "
           "tls_heap_min=%lu ble=%s",
           k_jobs[j].name, (long)r, (unsigned)http, (unsigned)ji.st, (unsigned long)(t1 - t0), (unsigned long)hw.largest,
           (unsigned long)hw.freeh, (unsigned long)n->tls_largest_min, (unsigned long)n->tls_heap_min,
           ble_is_running() ? "on" : "off");
  if (r != NET_E_HEAP && (hw.largest < HEALTH_FREE_FLOOR || hw.freeh < HEALTH_FREE_FLOOR))
    aura_log("[net] WARN TLS session low point free=%lu largest=%lu < %u", (unsigned long)hw.freeh,
             (unsigned long)hw.largest, (unsigned)HEALTH_FREE_FLOOR);
}

// ---------------------------------------------------------------- portal ---
static PortalData s_pd;
static uint32_t s_portal_t0 = 0, s_portal_stop_at = 0, s_portal_heap0 = 0;

static void html_esc(String &o, const char *s) {
  for (; *s; s++) {
    switch (*s) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += *s;
    }
  }
}
static const char k_css[] =
    "<meta name=viewport content='width=device-width,initial-scale=1'><style>body{font-family:sans-serif;"
    "background:#0b1020;color:#e6e9f2;margin:0;padding:16px}h1{font-size:20px}fieldset{border:1px solid #2a3558;"
    "border-radius:10px;margin:0 0 14px;padding:10px}legend{color:#7fb6ff}label{display:block;font-size:13px;"
    "margin:8px 0 3px;color:#aab}input{width:100%;box-sizing:border-box;padding:9px;border-radius:7px;border:1px "
    "solid #34406a;background:#141b33;color:#fff;font-size:15px}button{width:100%;padding:12px;border:0;"
    "border-radius:9px;background:#3d8bff;color:#fff;font-size:16px}small{color:#889}</style>";
static void page_root(void) {
  char ssid[WIFI_SSID_MAX + 1];
  size_t plen = 0;
  wifi_creds_peek(ssid, sizeof(ssid), &plen);
  size_t dsl = 0, dpl = 0;
  wifi_default_lens(&dsl, &dpl);  // built-in default: lengths only, the values are never sent
  String h;
  h.reserve(5600);  // p0p1: +Gemini key/model fields (~300 B)
  h += "<!doctype html><html><head><title>AURA setup</title>";
  h += k_css;
  h += "</head><body><h1>AURA " AURA_VERSION " setup</h1><form method=post action=/save>";
  h += "<fieldset><legend>Wi-Fi</legend><label>Network name (SSID)</label><input name=ssid maxlength=32 value=\"";
  html_esc(h, ssid);
  char nb[64];
  if (!ssid[0] && dsl) {  // only the built-in default exists: never prefill it
    snprintf(nb, sizeof(nb), "built-in default set (len %u) - leave empty to keep", (unsigned)dsl);
    h += "\" placeholder=\"";
    h += nb;
  }
  h += "\"><label>Password</label><input name=pass type=password maxlength=64 autocomplete=off placeholder=\"";
  if (plen) h += "saved - leave empty to keep";
  else if (!ssid[0] && dsl) { snprintf(nb, sizeof(nb), "built-in default set (len %u)", (unsigned)dpl); h += nb; }
  else h += "empty = open network";
  h += "\"></fieldset><fieldset><legend>API keys (optional)</legend><small>Stored on the device only. Keys are "
       "never shown (length only); leave a field empty to keep it.</small>";
  static const char *const lab[NKEYS] = {"Stocks key", "Cricket key", "Grok API key (xAI)", "Groq API key",
                                         "OpenWeatherMap key", "Gemini API key (Google)"};
  for (int i = 0; i < NKEYS; i++) {
    uint8_t src = 0;
    const size_t kl = key_len(i, &src);
    h += "<label>";
    h += lab[i];
    if (src) {
      snprintf(nb, sizeof(nb), " - set (len %u%s)", (unsigned)kl, src == 2 ? ", built-in" : "");
      h += nb;
    }
    h += "</label><input name=k_";
    h += k_key_names[i];
    h += " type=password maxlength=128 autocomplete=off>";
  }
  h += "<label>Grok model</label><input name=or_model maxlength=31 autocomplete=off value=\"";
  html_esc(h, s_xai_model);
  h += "\" placeholder=\"" ORACLE_MODEL_DEFAULT "\"><label>Groq model</label><input name=or_gmodel maxlength=31 "
       "autocomplete=off value=\"";
  html_esc(h, s_groq_model);
  h += "\" placeholder=\"" GROQ_MODEL_DEFAULT "\"><label>Gemini model</label><input name=or_mmodel maxlength=31 "
       "autocomplete=off value=\"";
  html_esc(h, s_gem_model);
  h += "\" placeholder=\"" GEMINI_MODEL_DEFAULT "\"><label>Oracle provider (now: ";
  h += s_or.provider ? "PC app" : s_or.api == ORA_GROQ ? "Groq" : s_or.api == ORA_GEMINI ? "Gemini" : "xAI";
  h += ")</label><select name=or_prov style='width:100%;padding:9px;border-radius:7px;background:#141b33;color:#fff'>";
  // option order = ORA_SEL_* (0..3), then the PC app
  static const char *const pv[5] = {"auto", "xai", "groq", "gemini", "pc"};
  static const char *const pl[5] = {"Auto: Gemini, else xAI, else Groq (first key set)", "This device: xAI Grok",
                                    "This device: Groq", "This device: Google Gemini", "AURA PC app"};
  const int cur = s_or.provider ? 4 : s_or.api_sel;
  for (int i = 0; i < 5; i++) {
    h += "<option value=";
    h += pv[i];
    h += i == cur ? " selected>" : ">";
    h += pl[i];
    h += "</option>";
  }
  h += "</select>";
  h += "<label><input name=clear_keys type=checkbox value=1 style='width:auto'> Remove all saved keys (built-in "
       "defaults stay)</label>";
  h += "</fieldset><fieldset><legend>Location</legend><label>City label</label><input name=city maxlength=23 value=\"";
  html_esc(h, s_loc.city);
  char b[24];
  h += "\"><label>Latitude</label><input name=lat inputmode=decimal value=\"";
  snprintf(b, sizeof(b), "%.3f", (double)s_loc.lat);
  h += b;
  h += "\"><label>Longitude</label><input name=lon inputmode=decimal value=\"";
  snprintf(b, sizeof(b), "%.3f", (double)s_loc.lon);
  h += b;
  h += "\"></fieldset><button>Save</button></form></body></html>";
  s_web->sendHeader("Cache-Control", "no-store");
  s_web->send(200, "text/html", h);
}
static void page_save(void) {
  bool wifi_ok = true, wifi_changed = false, loc_ok = true;
  unsigned keys_set = 0;
  {
    // Empty password must never write an "open" SSID over secrets.h defaults.
    // That NVS row wins on boot and WPA-PSK fails, which looks like a lost save.
    String ssid = s_web->arg("ssid");
    String pass = s_web->arg("pass");
    ssid.trim();
    char cur[WIFI_SSID_MAX + 1];
    size_t plen = 0;
    const bool have_nvs = wifi_creds_peek(cur, sizeof(cur), &plen);
    size_t dsl = 0, dpl = 0;
    wifi_default_lens(&dsl, &dpl);
    if (ssid.length()) {
      if (pass.length() == 0 && have_nvs && plen && ssid == cur) {
        wifi_changed = false;  // same network, keep the stored password
      } else if (pass.length() == 0 && !plen) {
        if (wifi_creds_from_default()) {
          wifi_ok = wifi_creds_materialize();
          wifi_changed = wifi_ok;
        } else if (dsl && WIFI_SSID_DEFAULT[0] && ssid == WIFI_SSID_DEFAULT) {
          wifi_ok = wifi_creds_save(WIFI_SSID_DEFAULT, WIFI_PASS_DEFAULT);
          wifi_changed = wifi_ok;
        } else {
          wifi_ok = false;  // need an explicit 8-64 character password
        }
      } else {
        wifi_ok = wifi_creds_save(ssid.c_str(), pass.c_str());
        wifi_changed = wifi_ok;
      }
      aura_log("[portal] wifi %s (ssid_len=%u pass_len=%u nvs=%d)", !wifi_ok ? "REJECTED" : wifi_changed ? "saved" : "kept",
               (unsigned)ssid.length(), (unsigned)pass.length(), have_nvs ? 1 : 0);
    } else if (wifi_creds_from_default() || (dsl && !have_nvs)) {
      // Fields left blank: still persist the live/default pair so a later empty
      // secrets.h (or an app-only flash) reconnects from NVS.
      wifi_ok = wifi_creds_materialize();
      wifi_changed = wifi_ok;
      aura_log("[portal] wifi %s (blank fields, materialize)", wifi_ok ? "saved" : "REJECTED");
    }
    if (pass.length()) wipe(&pass[0], pass.length());  // v3.1.2: volatile wipe, then drop the copy
    pass = String();
  }
  if (s_web->arg("clear_keys") == "1") {
    for (int i = 0; i < NKEYS; i++) net_key_set(k_key_names[i], "");
    Preferences p;  // P016: drop the orphaned pre-P016 football row too
    if (p.begin("keys", false)) { p.remove(k_key_legacy); p.end(); }
  }
  for (int i = 0; i < NKEYS; i++) {
    char an[16];
    snprintf(an, sizeof(an), "k_%s", k_key_names[i]);
    String v = s_web->arg(an);
    v.trim();
    if (v.length() && net_key_set(k_key_names[i], v.c_str())) keys_set++;
    if (v.length()) wipe(&v[0], v.length());  // v3.1.2: volatile wipe of the key copy
    v = String();
  }
  bool or_ok = true;
  if (s_web->hasArg("or_model") || s_web->hasArg("or_gmodel") || s_web->hasArg("or_mmodel") || s_web->hasArg("or_prov")) {
    auto model_ok = [](const String &m) {
      bool valid = m.length() <= 31;
      for (size_t i = 0; valid && i < m.length(); i++) {
        const char ch = m[i];
        valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_';
      }
      return valid;
    };
    String m = s_web->arg("or_model"), g = s_web->arg("or_gmodel"), mm = s_web->arg("or_mmodel");
    m.trim();
    g.trim();
    mm.trim();
    const bool vm = model_ok(m), vg = model_ok(g), vmm = model_ok(mm);
    int prov = -1, dprov = -1;
    if (s_web->hasArg("or_prov")) {
      const String pv = s_web->arg("or_prov");
      if (pv == "pc" || pv == "1") prov = 1;
      else {
        prov = 0;
        dprov = pv == "xai" ? ORA_SEL_XAI : pv == "groq" ? ORA_SEL_GROQ : pv == "gemini" ? ORA_SEL_GEMINI : ORA_SEL_AUTO;
      }
    }
    const char *ms = !vm || !s_web->hasArg("or_model") ? nullptr : m == ORACLE_MODEL_DEFAULT ? "" : m.c_str();
    const char *gs = !vg || !s_web->hasArg("or_gmodel") ? nullptr : g == GROQ_MODEL_DEFAULT ? "" : g.c_str();
    const char *mms = !vmm || !s_web->hasArg("or_mmodel") ? nullptr : mm == GEMINI_MODEL_DEFAULT ? "" : mm.c_str();
    or_ok = or_cfg_save(ms, gs, mms, prov, dprov) && vm && vg && vmm;
    static const char *const sel[4] = {"auto", "xai", "groq", "gemini"};
    aura_log("[portal] oracle xai_model=%s groq_model=%s gemini_model=%s provider=%s select=%s",
             vm ? (m.length() ? m.c_str() : ORACLE_MODEL_DEFAULT) : "REJECTED",
             vg ? (g.length() ? g.c_str() : GROQ_MODEL_DEFAULT) : "REJECTED",
             vmm ? (mm.length() ? mm.c_str() : GEMINI_MODEL_DEFAULT) : "REJECTED", prov < 0 ? "kept" : prov ? "pc" : "device",
             dprov < 0 ? "kept" : sel[dprov]);
  }
  {
    String city = s_web->arg("city"), la = s_web->arg("lat"), lo = s_web->arg("lon");
    city.trim();
    char c[24];
    size_t o = 0;
    for (size_t i = 0; i < city.length() && o < sizeof(c) - 1; i++) {
      const char ch = city[i];
      if (ch >= 0x20 && ch < 0x7F) c[o++] = ch;
    }
    c[o] = 0;
    char *e1 = nullptr, *e2 = nullptr;
    const float lat = strtof(la.c_str(), &e1), lon = strtof(lo.c_str(), &e2);
    if (la.length() && lo.length()) {
      loc_ok = e1 != la.c_str() && e2 != lo.c_str() && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
      if (loc_ok) loc_ok = loc_save(lat, lon, o ? c : LOC_DEFAULT_CITY);
    }
  }
  aura_log("[portal] saved wifi=%s keys_set=%u loc=%s", !wifi_ok ? "invalid" : wifi_changed ? "changed" : "unchanged",
           keys_set, loc_ok ? "ok" : "invalid");
  String h;
  h += "<!doctype html><html><head><title>AURA setup</title>";
  h += k_css;
  h += "</head><body><h1>";
  h += wifi_ok && loc_ok && or_ok ? "Saved" : "Saved with problems";
  h += "</h1><p>";
  if (!wifi_ok) h += "Wi-Fi was NOT changed: SSID 1-32 chars; password 8-64 chars (leave blank only to keep a password already saved on the device).<br>";
  if (!or_ok) h += "A model name was NOT changed: letters, digits, - . _ only (max 31).<br>";
  if (!loc_ok) h += "Location was NOT changed: latitude -90..90, longitude -180..180.<br>";
  h += "AURA is closing this setup network and reconnecting. You can close this page.</p></body></html>";
  s_web->send(200, "text/html", h);
  s_pd.saved = true;
  s_portal_stop_at = millis() + 2500;
}
static void page_redirect(void) {
  s_web->sendHeader("Location", "http://192.168.4.1/", true);
  s_web->send(302, "text/plain", "");
}
static void portal_publish(void) { ds_set_portal(&s_pd, 10); }
static void portal_start(uint32_t now) {
  if (s_web) return;
  s_portal_heap0 = heap_caps_get_free_size(NET_HEAP_CAPS);
  wifi_mgr_suspend();
  wipe(&s_pd, sizeof(s_pd));  // any previous session password (volatile wipe)
  // Q01: random per-session SSID suffix (esp_random, RF-seeded TRNG), not MAC-derived
  snprintf(s_pd.ap_ssid, sizeof(s_pd.ap_ssid), "AURA-Setup-%04X", (unsigned)(esp_random() & 0xFFFFu));
  static const char al[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0, o = 0; i < 8; i++) {
    if (i == 4) s_pd.ap_pass[o++] = '-';
    s_pd.ap_pass[o++] = al[esp_random() % (sizeof(al) - 1)];
  }
  wifi_preinit();  // v3.1.3 trimmed Wi-Fi buffers for the portal too
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  const bool ap = WiFi.softAP(s_pd.ap_ssid, s_pd.ap_pass, 6, 0, 2);
  s_dns = new DNSServer();
  s_web = new WebServer(80);
  s_dns->setErrorReplyCode(DNSReplyCode::NoError);
  s_dns->start(53, "*", IPAddress(192, 168, 4, 1));
  s_web->on("/", HTTP_GET, page_root);
  s_web->on("/save", HTTP_POST, page_save);
  // v3.1.6 p0p1 net slim: the 8 captive-probe routes (/generate_204, /hotspot-detect.html, /ncsi.txt, ...)
  // were exact copies of the not-found handler (302 to /, any method); dropped: same reply, 8 fewer
  // RequestHandler + Uri heap objects per portal session and their strings.
  s_web->onNotFound(page_redirect);  // every other path (captive probes included) -> 302 /; no /update endpoint
  s_web->begin();
  s_pd.on = ap;
  s_portal_t0 = now;
  s_portal_stop_at = 0;
  portal_publish();
  aura_log("[portal] start ap=%s ssid=%s (WPA2, password on screen only) ip=192.168.4.1 heap %lu -> %lu",
           ap ? "ok" : "FAILED", s_pd.ap_ssid, (unsigned long)s_portal_heap0,
           (unsigned long)heap_caps_get_free_size(NET_HEAP_CAPS));
}
static void portal_stop(const char *why) {
  if (!s_web) return;
  s_web->stop();
  s_dns->stop();
  delete s_web;
  delete s_dns;
  s_web = nullptr;
  s_dns = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  const bool saved = s_pd.saved;
  wipe(&s_pd, sizeof(s_pd));  // drops the AP password (volatile wipe, not memset)
  s_pd.saved = saved;
  portal_publish();
  wifi_mgr_resume();
  s_keys_dirty = true;
  s_loc_dirty = true;
  const uint32_t f = heap_caps_get_free_size(NET_HEAP_CAPS);
  aura_log("[portal] stop reason=%s saved=%d heap now %lu (before start %lu, net %ld)", why, saved ? 1 : 0,
           (unsigned long)f, (unsigned long)s_portal_heap0, (long)f - (long)s_portal_heap0);
}
static void portal_poll(uint32_t now) {
  s_dns->processNextRequest();
  s_web->handleClient();
  const uint8_t c = (uint8_t)WiFi.softAPgetStationNum();
  if (c != s_pd.clients) { s_pd.clients = c; portal_publish(); }
  if (s_portal_stop_at && (int32_t)(now - s_portal_stop_at) >= 0) portal_stop("saved");
  else if (now - s_portal_t0 > PORTAL_TIMEOUT_MS) portal_stop("timeout");
}

// ------------------------------------------------------- local companion ---
// TCP listener on the STA address only. One client. Pairing token is shown on
// the device (Settings > Companion, or PC Status while unpaired) and stored in
// NVS "comp"; it is never logged. The laptop's client id is bound on the first
// good HELLO and rejected for anyone else until forget (rotates the token) —
// AURA-only pairing. Feeds CompData for the PC Status app (cpu/ram/note=notif).
// v3.1.6 Now Playing: NOW <title>|<artist> and ART <w> <h> <n> + n raw bytes
// (RGB565 LE) feed CompNowData + the data_store art buffer. Titles/artists/pixels
// are never logged (lengths and sizes only).
static char s_tok[COMP_TOKEN_LEN + 1];
static char s_peer[COMP_ID_LEN + 1];
static char s_note[COMP_NOTE_MAX + 1];
static char s_line[160];
static uint8_t s_ln = 0;
static uint8_t s_cpu = 0, s_ram = 0;
static uint32_t s_stat_ms = 0;
static uint32_t s_tok_ms = 0;
static uint32_t s_bound_ip = 0;
static bool s_tok_loaded = false;
static bool s_authed = false;
static bool s_listen = false;
static volatile bool s_comp_on = false;
static volatile bool s_forget = false;
static WiFiServer s_csrv(COMP_PORT);
static WiFiClient s_ccli;
static char s_now_title[COMP_NOW_FIELD_MAX + 1];
static char s_now_artist[COMP_NOW_FIELD_MAX + 1];
static bool s_now_playing = false;
static uint32_t s_now_ms = 0;
static bool s_art_have = false;
static uint16_t s_art_need = 0;
static uint16_t s_art_got = 0;
static uint8_t s_art_w = 0, s_art_h = 0;
static uint32_t s_art_t0 = 0;

void net_comp_enable(bool on) { s_comp_on = on; }
void net_comp_forget(void) { s_forget = true; }

static void comp_wipe_line(void) {
  volatile char *v = s_line;
  for (size_t i = 0; i < sizeof(s_line); i++) v[i] = 0;
  s_ln = 0;
}
static bool comp_tok_eq(const char *a, const char *b) {
  uint8_t d = 0;
  for (int i = 0; i < COMP_TOKEN_LEN; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0 && a[COMP_TOKEN_LEN] == 0 && b[COMP_TOKEN_LEN] == 0;
}
static void comp_save_peer(void) {
  Preferences p;
  if (!p.begin("comp", false)) return;
  if (s_peer[0]) p.putString("peer", s_peer);
  else p.remove("peer");
  p.end();
}
static void comp_mint(void) {
  static const char alph[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (int i = 0; i < COMP_TOKEN_LEN; i++) s_tok[i] = alph[esp_random() % (sizeof(alph) - 1)];
  s_tok[COMP_TOKEN_LEN] = 0;
  Preferences p;
  if (p.begin("comp", false)) {
    p.putString("tok", s_tok);
    p.end();
  }
  s_tok_ms = millis();
  if (!s_tok_ms) s_tok_ms = 1;
  aura_log("[comp] new pairing code (len %u)", (unsigned)COMP_TOKEN_LEN);
}
static void comp_load(void) {
  if (s_tok_loaded) return;
  s_tok_loaded = true;
  Preferences p;
  if (p.begin("comp", true)) {
    p.getString("tok", s_tok, sizeof(s_tok));
    p.getString("peer", s_peer, sizeof(s_peer));
    p.end();
  }
  if (strlen(s_tok) != COMP_TOKEN_LEN) s_tok[0] = 0;
  if (strlen(s_peer) != COMP_ID_LEN) s_peer[0] = 0;
  if (s_tok[0] && !s_tok_ms) {
    s_tok_ms = millis();
    if (!s_tok_ms) s_tok_ms = 1;
  }
}
static void comp_now_clear(void) {
  s_now_title[0] = s_now_artist[0] = 0;
  s_now_playing = false;
  s_now_ms = 0;
  if (s_art_have || s_art_need) {
    if (!ds_comp_art_clear(10)) ds_comp_art_begin(10);
    s_art_have = false;
  }
  s_art_need = s_art_got = 0;
  s_art_w = s_art_h = 0;
}
static void comp_drop(void) {
  if (s_ccli) s_ccli.stop();
  s_authed = false;
  comp_wipe_line();
  comp_now_clear();
}
static void comp_listen_stop(void) {
  comp_drop();
  if (s_listen) {
    s_csrv.end();
    s_listen = false;
    s_bound_ip = 0;
  }
}
static void comp_publish(uint32_t ip, bool listening) {
  CompData c;
  memset(&c, 0, sizeof(c));
  c.on = s_comp_on;
  c.listening = listening;
  c.paired = s_peer[0] != 0;
  c.session = s_authed && (bool)s_ccli && s_ccli.connected();
  const bool fresh = s_stat_ms != 0 && (uint32_t)(millis() - s_stat_ms) < COMP_STALE_MS;
  c.live = c.session && fresh;
  c.cpu = s_cpu;
  c.ram = s_ram;
  c.ms = s_stat_ms;
  c.ip = listening ? ip : 0;
  if (s_comp_on) memcpy(c.token, s_tok, sizeof(s_tok));
  memcpy(c.note, s_note, sizeof(s_note));
#ifndef AURA_HOST
  {
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(c.devid, sizeof(c.devid), "AURA-%04X-%02X", (unsigned)((mac >> 16) & 0xFFFFu), (unsigned)(mac & 0xFFu));
  }
#else
  snprintf(c.devid, sizeof(c.devid), "AURA-HOST-00");
#endif
  c.tok_ms = s_tok_ms;
  ds_set_comp(&c, 10);
  CompNowData nd;
  memset(&nd, 0, sizeof(nd));
  nd.playing = s_now_playing;
  nd.ms = s_now_ms;
  nd.live = s_now_playing && c.session && s_now_ms != 0 && (uint32_t)(millis() - s_now_ms) < COMP_NOW_STALE_MS;
  memcpy(nd.title, s_now_title, sizeof(nd.title));
  memcpy(nd.artist, s_now_artist, sizeof(nd.artist));
  ds_set_comp_now(&nd, 10);
}
static void comp_reply(const char *s) {
  if (s_ccli) s_ccli.print(s);
}
static void comp_on_line(void) {
  CompLine msg;
  const bool ok = comp_parse(s_line, &msg);
  comp_wipe_line();
  if (!ok) {
    comp_reply("DENY\n");
    comp_drop();
    return;
  }
  if (!s_authed) {
    if (msg.kind != COMP_HELLO || !comp_tok_eq(msg.token, s_tok) || (s_peer[0] && strcmp(s_peer, msg.id) != 0)) {
      aura_log("[comp] deny");
      comp_reply("DENY\n");
      comp_drop();
      return;
    }
    if (!s_peer[0]) {
      memcpy(s_peer, msg.id, COMP_ID_LEN + 1);
      comp_save_peer();
      aura_log("[comp] bound one laptop");
    }
    s_authed = true;
    comp_reply("OK\n");
    return;
  }
  if (msg.kind == COMP_NOW) {
    memcpy(s_now_title, msg.title, sizeof(s_now_title));
    memcpy(s_now_artist, msg.artist, sizeof(s_now_artist));
    s_now_playing = msg.title[0] || msg.artist[0];
    s_now_ms = millis();
    if (s_now_playing) aura_log("[comp] now (t%u a%u)", (unsigned)strlen(s_now_title), (unsigned)strlen(s_now_artist));
    else aura_log("[comp] now clear");
    comp_reply("ACK\n");
    return;
  }
  if (msg.kind == COMP_ART) {
    if (msg.art_n == 0) {
      if (s_art_have) {
        if (!ds_comp_art_clear(10)) {
          comp_reply("DENY\n");
          comp_drop();
          return;
        }
        s_art_have = false;
      }
      s_now_ms = millis();
      aura_log("[comp] art clear");
      comp_reply("ACK\n");
      return;
    }
    if (!ds_comp_art_begin(10)) {
      comp_reply("DENY\n");
      comp_drop();
      return;
    }
    s_art_have = false;
    s_art_w = msg.art_w;
    s_art_h = msg.art_h;
    s_art_need = msg.art_n;
    s_art_got = 0;
    s_art_t0 = millis();
    return;
  }
  if (msg.kind == COMP_NOTIF) {
    memcpy(s_note, msg.note, sizeof(s_note));
    s_stat_ms = millis();
    comp_reply("ACK\n");
    return;
  }
  if (msg.kind != COMP_STAT) {
    comp_reply("DENY\n");
    comp_drop();
    return;
  }
  s_cpu = msg.cpu;
  s_ram = msg.ram;
  memcpy(s_note, msg.note, sizeof(s_note));
  s_stat_ms = millis();
  comp_reply("ACK\n");
}
static bool comp_art_rx(void) {
  uint8_t buf[256];
  while (s_art_got < s_art_need && s_ccli.available() > 0) {
    uint16_t want = s_art_need - s_art_got;
    if (want > sizeof(buf)) want = sizeof(buf);
    const int r = s_ccli.read(buf, want);
    if (r <= 0) break;
    if (!ds_comp_art_write(s_art_got, buf, (uint16_t)r, 10)) {
      aura_log("[comp] art store busy");
      comp_reply("DENY\n");
      comp_drop();
      return false;
    }
    s_art_got += (uint16_t)r;
  }
  if (s_art_got < s_art_need) return true;
  s_art_need = s_art_got = 0;
  if (!ds_comp_art_commit(s_art_w, s_art_h, 10)) {
    comp_reply("DENY\n");
    comp_drop();
    return false;
  }
  s_art_have = true;
  s_now_ms = millis();
  aura_log("[comp] art %ux%u", (unsigned)s_art_w, (unsigned)s_art_h);
  comp_reply("ACK\n");
  return true;
}
static void comp_poll(uint32_t now, bool portal, bool sta_up, uint32_t ip) {
  comp_load();
  if (s_forget) {
    s_forget = false;
    s_peer[0] = 0;
    comp_save_peer();
    s_note[0] = 0;
    s_stat_ms = 0;
    comp_mint();
    comp_drop();  // also clears Now Playing + art
    aura_log("[comp] pair cleared");
  }
  if (s_comp_on && !s_tok[0]) comp_mint();
  if (!s_comp_on || portal || !sta_up || !ip) {
    comp_listen_stop();
    comp_publish(0, false);
    return;
  }
  if (!s_listen || ip != s_bound_ip) {
    comp_listen_stop();
    s_csrv.begin();
    s_listen = true;
    s_bound_ip = ip;
    aura_log("[comp] listen port %u", (unsigned)COMP_PORT);
  }
  if (s_ccli && s_ccli.connected()) {
    if (s_art_need && (uint32_t)(millis() - s_art_t0) > COMP_ART_RX_MS) {
      aura_log("[comp] art timeout");
      comp_reply("DENY\n");
      comp_drop();
    }
    while (s_ccli && s_ccli.available() > 0) {
      if (s_art_need) {
        if (!comp_art_rx() || s_art_need) break;
        continue;
      }
      const int ch = s_ccli.read();
      if (ch < 0) break;
      if (ch == '\n') {
        if (s_ln && s_line[s_ln - 1] == '\r') s_line[--s_ln] = 0;
        s_line[s_ln] = 0;
        comp_on_line();
        if (!s_ccli || !s_ccli.connected()) break;
      } else if (s_ln + 1 >= sizeof(s_line)) {
        comp_reply("DENY\n");
        comp_drop();
        break;
      } else s_line[s_ln++] = (char)ch;
    }
  } else {
    if (s_ccli || s_art_need) comp_drop();
    WiFiClient n = s_csrv.accept();
    if (n) {
      n.setNoDelay(true);
      s_ccli = n;
      s_authed = false;
      comp_wipe_line();
    }
  }
  comp_publish(ip, s_listen);
  (void)now;
}

// ----------------------------------------------------------------- task ---
static void net_task(void *) {
  NetData n;
  memset(&n, 0, sizeof(n));
  n.tls_largest_min = UINT32_MAX;
  n.tls_heap_min = UINT32_MAX;
  memset(s_job, 0, sizeof(s_job));
  esp_timer_create_args_t ta = {};
  ta.callback = heap_sample_cb;
  ta.dispatch_method = ESP_TIMER_TASK;
  ta.name = "tls_heap";
  if (esp_timer_create(&ta, &s_heap_timer) != ESP_OK) {
    s_heap_timer = nullptr;
    aura_log("[net] heap sample timer FAILED (TLS low point = endpoints + min-ever delta only)");
  }
  if (!wifi_mgr_init()) aura_log("[net] wifi init FAILED");
  bool was_up = false, time_ok = false;
  uint32_t tz_seen = 0;
  for (;;) {
    const uint32_t now = millis();
    if (tz_seen != tz_gen()) {
      tz_seen = tz_gen();
      const char *z = tz_om(tz_get());
      if (!z || strlen(z) >= sizeof(s_wx_tz)) z = AURA_TZ_OPENMETEO;
      snprintf(s_wx_tz, sizeof(s_wx_tz), "%s", z);
      configTzTime(tz_posix(tz_get()), "pool.ntp.org", "time.google.com");
      aura_log("[net] timezone %s", tz_label(tz_get()));
    }
    if (s_keys_dirty) { s_keys_dirty = false; keys_publish(); s_or_cfg_dirty = true; }  // re-resolve the Oracle API
    if (s_or_cfg_dirty) { s_or_cfg_dirty = false; or_cfg_load(); }
    if (s_loc_dirty) {
      s_loc_dirty = false;
      loc_load();
      for (int j = 0; j < JOB_COUNT; j++) s_job[j].next_ms = now;  // new location -> refetch
    }
    const int8_t pr = s_portal_req;
    if (pr >= 0) {
      s_portal_req = -1;
      if (pr == 1) { s_pd.saved = false; portal_start(now); }
      else portal_stop("user");
    }
    if (s_web) {
      portal_poll(now);
      n.wifi_state = WIFI_ST_BACKOFF;
      n.rssi = 0;
      n.ip = 0;
      was_up = false;
      const uint32_t rm = __atomic_exchange_n(&s_refresh_mask, 0u, __ATOMIC_RELAXED);
      for (int j = 0; j < JOB_COUNT; j++) {
        if (!job_armed(j)) continue;  // no key: leave JS_NEVER, the screen shows "add key"
        if ((rm & (1u << j)) || (int32_t)(now - s_job[j].next_ms) >= 0)
          if (s_job[j].st != JS_PAUSED && s_job[j].st != JS_OK) { s_job[j].st = JS_PAUSED; ds_set_job((uint8_t)j, &s_job[j], 10); }
      }
      if (oracle_pending()) { job_oracle(&n, false); ui_cache_resume(); }
      comp_poll(now, true, false, 0);
      wifi_ssid_copy(n.ssid, sizeof(n.ssid)); ds_set_net(&n, 10);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const WifiState st = wifi_mgr_poll(now);
    n.wifi_state = st;
    if (st == WIFI_ST_UP) {
      n.rssi = (int8_t)WiFi.RSSI();
      n.ip = (uint32_t)WiFi.localIP();
      if (!was_up) {
        for (int j = 0; j < JOB_COUNT; j++) {  // stagger the first round after (re)connect
          if (s_job[j].st != JS_OK || (int32_t)(now - s_job[j].next_ms) >= 0) s_job[j].next_ms = now + 400u * (uint32_t)j;
          s_job[j].attempt = 0;
        }
      }
    } else {
      n.rssi = 0;
      n.ip = 0;
    }
    was_up = (st == WIFI_ST_UP);
    comp_poll(now, false, st == WIFI_ST_UP, n.ip);
    if (!time_ok && time(nullptr) > 1700000000) {
      time_ok = true;
      ds_set_time_valid(true, 10);
      aura_log("[net] time synced (%s)", tz_label(tz_get()));
    }
    const uint32_t rm = __atomic_exchange_n(&s_refresh_mask, 0u, __ATOMIC_RELAXED);
    for (int j = 0; j < JOB_COUNT; j++)
      if (rm & (1u << j)) { s_job[j].next_ms = now; if (s_job[j].st == JS_FAIL || s_job[j].st == JS_HEAP) s_job[j].attempt = 0; }
    if (oracle_pending()) {
      // the Oracle question goes first (it queued behind, or preempted, the feed job)
      wifi_ssid_copy(n.ssid, sizeof(n.ssid)); ds_set_net(&n, 10);
      job_oracle(&n, st == WIFI_ST_UP);
      ui_cache_resume();
    } else if (st == WIFI_ST_UP) {
      // one TLS job at a time: the most overdue one; the weather job first on ties
      int pick = -1;
      int32_t best = 0;
      for (int j = 0; j < JOB_COUNT; j++) {
        if (!job_armed(j)) continue;
        const int32_t od = (int32_t)(now - s_job[j].next_ms);
        if (od >= 0 && (pick < 0 || od > best)) { pick = j; best = od; }
      }
      if (pick >= 0) {
        wifi_ssid_copy(n.ssid, sizeof(n.ssid)); ds_set_net(&n, 10);
        run_job(pick, &n, now);
      }
    }
    wifi_ssid_copy(n.ssid, sizeof(n.ssid)); ds_set_net(&n, 10);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool net_task_start(void) {
  // Stack + TCB come from the heap ONCE at boot (not steady state).
  if (xTaskCreatePinnedToCore(net_task, "net", NET_TASK_STACK, nullptr, NET_TASK_PRIO, &s_task, NET_TASK_CORE) != pdPASS) s_task = nullptr;
  return s_task != nullptr;
}

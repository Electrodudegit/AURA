// setup_cli.cpp - line-based serial CLI. Input is NOT echoed (passwords).
//   wifi set <ssid> <pass>   (ssid may be "quoted with spaces"; pass = rest of line)
//   wifi clear | portal on|off | refresh | snap | cal | cal clear | tgrid | ble on|off
//   status | reboot | help
// v3.1.3 bench: face next|prev|1|2|5|6 | menu open|close | app <name> | app ? | back | home | heap |
//   prof on|off | oracle ask <text>   (none of them prints a key, password or the Oracle reply)
// API keys are entered in the setup portal only (never on the serial line).
#include "setup_cli.h"
#include "config.h"
#include "version.h"
#include "aura_log.h"
#include "wifi_mgr.h"
#include "data_store.h"
#include "ui_task.h"
#include "net_task.h"
#include "touch_xpt2046.h"
#include "touch_cal.h"
#include "health_log.h"
#include "ble_hid.h"
#include "ui_core.h"
#include <ctype.h>
#include <Arduino.h>
#include <string.h>

#define CLI_LINE_MAX 160
static char s_line[CLI_LINE_MAX + 1];
static size_t s_len = 0;
static bool s_overflow = false;

void cli_print_help(void) {
  aura_log("[cli] commands: wifi set <ssid> <pass> | wifi clear | portal on|off | refresh | snap | cal | cal clear | "
           "tgrid | ble on|off | status | reboot | help");
  aura_log("[cli] bench: face next|prev|1|2|5|6 | menu open|close | app <name> | app ? | back | home | heap | "
           "prof on|off | oracle ask <text>");
}

static void wipe_line(void) {
  memset(s_line, 0, sizeof(s_line));  // the line may hold a password
  s_len = 0;
}

static void cmd_wifi_set(char *args) {
  while (*args == ' ') args++;
  char *ssid = args;
  char *rest = nullptr;
  if (*args == '"') {
    ssid = args + 1;
    char *q = strchr(ssid, '"');
    if (!q) { aura_log("[cli] wifi set: missing closing quote"); return; }
    *q = '\0';
    rest = q + 1;
  } else {
    char *sp = strchr(args, ' ');
    if (sp) { *sp = '\0'; rest = sp + 1; }
    else rest = args + strlen(args);
  }
  while (*rest == ' ') rest++;
  const char *pass = rest;
  const size_t lp = strlen(pass);
  if (wifi_creds_save(ssid, pass)) aura_log("[cli] wifi saved ssid_len=%u pass_len=%u", (unsigned)strlen(ssid), (unsigned)lp);
  else aura_log("[cli] wifi set FAILED (ssid 1..32, pass empty or 8..64 chars)");
}

static void cmd_status(void) {
  AuraData &d = *health_scratch();  // v3.1.3: shared static snapshot (CLI runs in the ble task)
  if (!ds_get(&d, 50)) { aura_log("[status] data_store busy"); return; }
  char ssid[WIFI_SSID_MAX + 1];
  size_t plen = 0, slen = 0;
  const char *src = "none";
  if (wifi_creds_peek(ssid, sizeof(ssid), &plen)) {
    slen = strlen(ssid);
    src = "nvs";
  } else {
    size_t dl = 0, dp = 0;
    wifi_default_lens(&dl, &dp);
    slen = dl;
    plen = dp;
    src = dl ? "default" : "none";
  }
  memset(ssid, 0, sizeof(ssid));
  const uint32_t ip = d.net.ip;
  aura_log("[status] %s %s wifi_state=%u ssid_len=%u pass_len=%u src=%s ip=%lu.%lu.%lu.%lu rssi=%d", AURA_NAME, AURA_VERSION,
           (unsigned)d.net.wifi_state, (unsigned)slen, (unsigned)plen, src, (unsigned long)(ip & 0xFF),
           (unsigned long)((ip >> 8) & 0xFF), (unsigned long)((ip >> 16) & 0xFF), (unsigned long)(ip >> 24),
           (int)d.net.rssi);
  aura_log("[status] weather valid=%d temp_x10=%d http=%u ok=%lu fail=%lu err=%ld tls_skipped=%lu", d.net.temp_valid ? 1 : 0,
           d.net.temp_x10, (unsigned)d.net.http_status, (unsigned long)d.net.fetch_ok,
           (unsigned long)d.net.fetch_fail, (long)d.net.last_err, (unsigned long)d.net.tls_skipped);
  static const char *const jn[JOB_COUNT] = {"wx", "air", "crypto", "news", "stocks", "cricket"};
  for (int j = 0; j < JOB_COUNT; j++)
    aura_log("[status] job %s st=%u attempt=%u err=%d http=%u age=%lds", jn[j], (unsigned)d.job[j].st,
             (unsigned)d.job[j].attempt, (int)d.job[j].err, (unsigned)d.job[j].http,
             d.job[j].ok_ms ? (long)((millis() - d.job[j].ok_ms) / 1000u) : -1L);
  aura_log("[status] keys stocks=%d cricket=%d xai=%d groq=%d owm=%d gemini=%d default_mask=0x%02x (presence only) "
           "oracle=%s/%s loc=%.2f,%.2f '%s' time=%d",
           d.keys.stocks, d.keys.cricket, d.keys.oracle, d.keys.groq, d.keys.owm, d.keys.gemini, (unsigned)d.keys.dflt,
           d.oracle.provider ? "pc" : "device", ora_api_name(d.oracle.api), (double)d.loc.lat, (double)d.loc.lon,
           d.loc.city, d.time_valid ? 1 : 0);
  aura_log("[status] ble enabled=%d started=%d adv=%d adv_left=%lus conn=%u sent=%lu dropped=%lu", d.ble.enabled ? 1 : 0,
           d.ble.started ? 1 : 0, d.ble.advertising ? 1 : 0, (unsigned long)(d.ble.adv_left_ms / 1000u), (unsigned)d.ble.connected,
           (unsigned long)d.ble.hid_sent, (unsigned long)d.ble.hid_dropped);
  aura_log("[status] batt adc=%umV vbat=%umV pct=%d (BATT_DIVIDER=%.2f, assumption)", (unsigned)d.batt.adc_mv,
           (unsigned)d.batt.vbat_mv, (int)d.batt.pct, (double)BATT_DIVIDER);
  TouchStats ts;
  touch_get_stats(&ts);
  const TouchCal *c = touch_get_cal();
  // Split into short lines: each worst case stays well below AURA_LOG_MAX (no truncation).
  aura_log("[status] touch cal=%s k=%ld,%ld,%ld,%ld,%ld,%ld", touch_cal_from_nvs() ? "nvs" : "default",
           (long)c->k[0], (long)c->k[1], (long)c->k[2], (long)c->k[3], (long)c->k[4], (long)c->k[5]);
  aura_log("[status] touch reads=%lu skip=%lu fb=%lu spi_err=%lu irq_edges=%lu irq=%s", (unsigned long)ts.reads,
           (unsigned long)ts.skipped, (unsigned long)ts.fallback_reads, (unsigned long)ts.spi_err,
           (unsigned long)ts.irq_edges,
           touch_irq_status() == TOUCH_IRQ_SEEN ? "seen" : touch_irq_status() == TOUCH_IRQ_UNROUTED ? "unrouted" : "unknown");
  aura_log("[status] touch rej z=%lu range=%lu spread=%lu jump=%lu", (unsigned long)ts.rej_z,
           (unsigned long)ts.rej_range, (unsigned long)ts.rej_spread, (unsigned long)ts.rej_jump);
  health_print_now();
}

// ---- v3.1.3 bench commands ----
static bool ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}
static void cmd_app(const char *name) {
  while (*name == ' ') name++;
  if (strcmp(name, "?") == 0 || !*name) {
    char b[200];
    int o = snprintf(b, sizeof(b), "[cli] apps:");
    for (uint8_t i = 0; i < APP_COUNT; i++) {
      const AppDef *a = app_def(i);
      if (a && o < (int)sizeof(b) - 16) o += snprintf(b + o, sizeof(b) - o, " %s", a->name);
    }
    aura_log("%s", b);
    return;
  }
  for (uint8_t i = 0; i < APP_COUNT; i++) {
    const AppDef *a = app_def(i);
    if (a && ieq(a->name, name)) {
      const bool ok = ui_bench(UB_APP, i);
      aura_log("[cli] app %s %s", a->name, ok ? "queued" : "queue full");
      return;
    }
  }
  aura_log("[cli] app: unknown name (type: app ?)");
}
static void cmd_face(const char *a) {
  bool ok = false;
  if (strcmp(a, "next") == 0) ok = ui_bench(UB_FACE_STEP, 1);
  else if (strcmp(a, "prev") == 0) ok = ui_bench(UB_FACE_STEP, -1);
  else {
    static const int8_t k_idx[7] = {-1, 0, 1, -1, -1, 2, 3};  // F1, F2, F5, F6 (F3/F4 do not exist)
    const int f = (a[0] >= '1' && a[0] <= '6' && a[1] == 0) ? a[0] - '0' : 0;
    if (!f || k_idx[f] < 0) { aura_log("[cli] face: use next | prev | 1 | 2 | 5 | 6 (faces F1, F2, F5, F6)"); return; }
    ok = ui_bench(UB_FACE_SET, k_idx[f]);
  }
  aura_log("[cli] face %s %s", a, ok ? "queued" : "queue full");
}
static void cmd_oracle_ask(const char *q) {
  while (*q == ' ') q++;
  const size_t n = strlen(q);
  if (!n) { aura_log("[cli] oracle ask <text>"); return; }
  const uint32_t seq = net_oracle_ask(q);  // one real request on the device's own key; the reply is never printed
  aura_log("[cli] oracle ask seq=%lu prompt_len=%u heap=%lu largest=%lu (result: [oracle] seq=%lu ... lines)",
           (unsigned long)seq, (unsigned)n, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(),
           (unsigned long)seq);
}

#ifdef AURA_QEMU_NORADIO
#include <multi_heap.h>
struct QB { uint32_t sz[48], n[48]; int k; };
static bool qb_cb(walker_heap_into_t, walker_block_info_t bi, void *u) {
  QB *q = (QB *)u;
  if (!bi.used) return true;
  for (int i = 0; i < q->k; i++) if (q->sz[i] == bi.size) { q->n[i]++; return true; }
  if (q->k < 48) { q->sz[q->k] = (uint32_t)bi.size; q->n[q->k++] = 1; }
  return true;
}
static void qemu_blocks(void) {
  static QB q;
  memset(&q, 0, sizeof(q));
  heap_caps_walk(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, qb_cb, &q);
  for (int i = 0; i < q.k; i++) if (q.n[i] >= 8) aura_log("[qb] size=%lu n=%lu", (unsigned long)q.sz[i], (unsigned long)q.n[i]);
}
#endif

static void handle_line(char *l) {
  while (*l == ' ') l++;
  size_t n = strlen(l);
  while (n && (l[n - 1] == ' ' || l[n - 1] == '\r')) l[--n] = '\0';
  if (n == 0) return;
  if (strncmp(l, "wifi set ", 9) == 0) cmd_wifi_set(l + 9);
  else if (strcmp(l, "wifi clear") == 0) aura_log("[cli] wifi clear %s", wifi_creds_clear() ? "ok" : "FAILED");
  else if (strcmp(l, "cal") == 0) { ui_request_cal(); aura_log("[cli] opening Settings > Touch calibration"); }
  else if (strcmp(l, "cal clear") == 0) {
    const bool ok = touch_cal_erase();
    aura_log("[cli] cal clear %s (defaults after reboot)", ok ? "ok" : "FAILED");
  }
  else if (strcmp(l, "tgrid") == 0) { ui_request_grid(); aura_log("[cli] tap-grid test (Settings > Diagnostics) toggled"); }
  else if (strcmp(l, "status") == 0) cmd_status();
  else if (strcmp(l, "weather") == 0 || strcmp(l, "refresh") == 0) { net_request_refresh(-1); aura_log("[cli] refresh of all data jobs requested"); }
  else if (strcmp(l, "portal on") == 0) { net_portal(true); aura_log("[cli] setup portal start requested"); }
  else if (strcmp(l, "portal off") == 0) { net_portal(false); aura_log("[cli] setup portal stop requested"); }
  else if (strcmp(l, "snap") == 0) { ui_request_snap(); aura_log("[cli] snapshot requested (decode: tools/snap2png.py)"); }
  else if (strcmp(l, "ble on") == 0) { ble_request(true); aura_log("[cli] ble kick (enabled=%d)", ble_enabled() ? 1 : 0); }
  else if (strcmp(l, "ble off") == 0) { ble_request(false); aura_log("[cli] ble stop requested"); }
  else if (strcmp(l, "reboot") == 0) { aura_log("[cli] rebooting"); Serial.flush(); esp_restart(); }
  else if (strcmp(l, "help") == 0) cli_print_help();
  else if (strncmp(l, "face ", 5) == 0) cmd_face(l + 5);
  else if (strcmp(l, "menu open") == 0) aura_log("[cli] menu open %s", ui_bench(UB_MENU_OPEN, 0) ? "queued" : "queue full");
  else if (strcmp(l, "menu close") == 0) aura_log("[cli] menu close %s", ui_bench(UB_MENU_CLOSE, 0) ? "queued" : "queue full");
  else if (strcmp(l, "app") == 0 || strncmp(l, "app ", 4) == 0) cmd_app(l + 3);
  else if (strcmp(l, "back") == 0) aura_log("[cli] back %s", ui_bench(UB_BACK, 0) ? "queued" : "queue full");
  else if (strcmp(l, "home") == 0) aura_log("[cli] home %s", ui_bench(UB_HOME, 0) ? "queued" : "queue full");
  else if (strcmp(l, "heap") == 0) health_heap_dump();
  else if (strcmp(l, "prof on") == 0) { ui_prof_enable(true); aura_log("[cli] prof on"); }
  else if (strcmp(l, "prof off") == 0) { ui_prof_enable(false); aura_log("[cli] prof off"); }
  else if (strncmp(l, "oracle ask ", 11) == 0) cmd_oracle_ask(l + 11);
#ifdef AURA_QEMU_NORADIO
  else if (strcmp(l, "qlend") == 0) aura_log("[cli] qemu lend freed=%lu", (unsigned long)ui_cache_release(200));
  else if (strcmp(l, "qresume") == 0) { ui_cache_resume(); aura_log("[cli] qemu resume"); }
  else if (strcmp(l, "qblocks") == 0) qemu_blocks();
#endif
  else aura_log("[cli] unknown command (type: help)");  // do not echo the line: it may contain a secret
}

void cli_poll(void) {
  int budget = 64;  // bounded work per call
  while (budget-- > 0 && Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (s_overflow) aura_log("[cli] line too long, ignored");
      else if (s_len > 0) { s_line[s_len] = '\0'; handle_line(s_line); }
      s_overflow = false;
      wipe_line();
      continue;
    }
    if (s_len < CLI_LINE_MAX) s_line[s_len++] = (char)ch;
    else s_overflow = true;
  }
}

// apps_tools.cpp - local apps: Focus (pomodoro), World clocks, System, plus
// Oracle (key + PC-link gated) and Macro (BLE HID media keys).
#include "apps.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "version.h"
#include "config.h"

// ================================================================== Focus ==
FocusState g_focus;
static const uint8_t k_presets[3] = {15, 25, 50};
#define FOCUS_ROUNDS 4
static uint32_t focus_len_ms(void) { return (uint32_t)g_ui.prefs.focus_min * 60000u; }
static uint32_t break_len_ms(void) { return (g_focus.sessions % FOCUS_ROUNDS == 0 && g_focus.sessions) ? 15u * 60000u : 5u * 60000u; }
static uint32_t focus_left(void) {
  if (g_focus.st == 1 || g_focus.st == 3) return g_focus.t_end > g_ui.now ? g_focus.t_end - g_ui.now : 0;
  if (g_focus.st == 2) return g_focus.t_left;
  return focus_len_ms();
}
// Runs every frame from ui_frame_begin (the timer keeps going outside the app).
void focus_tick(uint32_t now) {
  if ((g_focus.st == 1 || g_focus.st == 3) && (int32_t)(now - g_focus.t_end) >= 0) {
    if (g_focus.st == 1) {
      g_focus.sessions++;
      g_focus.st = 3;
      g_focus.t_end = now + break_len_ms();
      ui_toast("Focus done - take a break", 3000);
      hal_log("[focus] session %u done, break %lu min", (unsigned)g_focus.sessions, (unsigned long)(break_len_ms() / 60000u));
    } else {
      g_focus.st = 0;
      ui_toast("Break over", 2500);
    }
  }
}
void focus_enter(uint32_t) {}
void focus_leave(void) {}
void focus_input(const UiIn &in, uint32_t now) {
  switch (in.type) {
    case IN_CLICK: case IN_TAP:
      if (g_focus.st == 0) { g_focus.st = 1; g_focus.t_end = now + focus_len_ms(); }
      else if (g_focus.st == 1) { g_focus.st = 2; g_focus.t_left = g_focus.t_end - now; }
      else if (g_focus.st == 2) { g_focus.st = 1; g_focus.t_end = now + g_focus.t_left; }
      else g_focus.st = 0;  // skip the break
      if (in.type == IN_TAP) ui_consume();
      break;
    case IN_ROT:
      if (g_focus.st == 0) {
        int i = 0;
        while (i < 3 && k_presets[i] != g_ui.prefs.focus_min) i++;
        i = (i + (in.d > 0 ? 1 : 2)) % 3;
        g_ui.prefs.focus_min = k_presets[i];
        ui_prefs_changed();
      }
      break;
    case IN_LONG:
      g_focus.st = 0;
      ui_toast("Timer reset", 1200);
      break;
    default: break;
  }
}
void focus_draw(void) {
  gfx_fill(C_BG);
  w_status("FOCUS", "");
  const float cx = 104, cy = 128, R = 80;
  const uint32_t left = focus_left();
  const uint32_t total = g_focus.st == 3 ? break_len_ms() : focus_len_ms();
  const float frac = total ? ui_clampf(left / (float)total, 0, 1) : 0;
  const uint16_t ac = g_focus.st == 3 ? C_UP : (g_focus.st == 2 ? C_WARN : C_ACC);
  if (gfx_vis(44, 170)) {
    gfx_arc(cx, cy, R, 8, 0, 360, C_TILE_HI);
    const float a0 = -90, a1 = -90 + 360 * frac;
    if (frac > 0.001f) gfx_arc(cx, cy, R, 8, a0, a1, ac);
    const float ae = a1 * 0.0174533f;
    gfx_circle(cx + cosf(ae) * (R - 4), cy + sinf(ae) * (R - 4), 6, C_TXT);
    char b[12];
    snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(left / 60000u), (unsigned long)((left / 1000u) % 60u));
    gfx_aatext_hero(FAA_B34N, (int)cx, (int)cy - 4, b, C_TXT, C_BG, C_ACC, 0);
    char s[24];
    if (g_focus.st == 3) snprintf(s, sizeof(s), "BREAK");
    else if (g_focus.st == 2) snprintf(s, sizeof(s), "PAUSED");
    else snprintf(s, sizeof(s), "FOCUS %u / %u", (unsigned)(g_focus.sessions % FOCUS_ROUNDS + 1), (unsigned)FOCUS_ROUNDS);
    gfx_micro((int)cx, (int)cy + 22, s, C_MUT, GA_C);
  }
  // right column
  gfx_micro(206, 47, "PRESET", C_MUT);
  for (int i = 0; i < 3; i++) {
    char b[4];
    snprintf(b, sizeof(b), "%u", (unsigned)k_presets[i]);
    const bool on = k_presets[i] == g_ui.prefs.focus_min;
    gfx_rrect(206 + i * 28, 61, 24, 14, 7, on ? C_TILE_HI : C_TILE);
    gfx_rrect_frame(206 + i * 28, 61, 24, 14, 7, on ? C_FOCUS_BORDER : C_LINE);
    gfx_micro(218 + i * 28, 64, b, on ? C_TXT : C_MUT, GA_C, true);
  }
  gfx_micro(206, 96, "SESSIONS TODAY", C_MUT);
  for (int i = 0; i < FOCUS_ROUNDS; i++)
    gfx_rrect(206 + i * 24, 111, 20, 7, 3, i < (int)(g_focus.sessions % FOCUS_ROUNDS) || (g_focus.sessions && g_focus.sessions % FOCUS_ROUNDS == 0 && g_focus.st == 3) ? C_ACC : C_TILE_HI);
  gfx_micro(206, 140, g_focus.st == 3 ? "BACK TO WORK IN" : "BREAK IN", C_MUT);
  {
    char b[16];
    const uint32_t m = (left + 59999u) / 60000u;
    snprintf(b, sizeof(b), "%lu min", (unsigned long)m);
    gfx_text_m(&AF_LABEL_M, 206, 160, b, C_TXT);
  }
  w_footer(g_focus.st == 1 ? "PRESS: PAUSE   LONG TAP: RESET   HOLD: BACK"
                           : g_focus.st == 0 ? "PRESS: START   TURN: PRESET   HOLD: BACK"
                                             : "PRESS: RESUME   LONG TAP: RESET   HOLD: BACK");
}

// ================================================================== World ==
// UTC offset in minutes with DST rules (EU / US), for t = UTC epoch.
static int dow(int y, int m, int d) {  // 0 = Sunday (Sakamoto)
  static const int tt[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + tt[m - 1] + d) % 7;
}
static int last_sunday(int y, int m, int days) { return days - dow(y, m, days); }
static int nth_sunday(int y, int m, int n) { const int f = 1 + (7 - dow(y, m, 1)) % 7; return f + 7 * (n - 1); }
static time_t utc_mk(int y, int m, int d, int h) {  // days-from-civil
  const int yy = y - (m <= 2);
  const int era = (yy >= 0 ? yy : yy - 399) / 400;
  const unsigned yoe = (unsigned)(yy - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + (long)doe - 719468L;
  return (time_t)(days * 86400L + h * 3600L);
}
int world_offset_min(uint8_t city, time_t t) {
  struct tm u;
  gmtime_r(&t, &u);
  const int y = u.tm_year + 1900;
  switch (city) {
    case 0: return 330;  // here: IST (device TZ)
    case 1: {            // London: BST from last Sun Mar 01:00 UTC to last Sun Oct 01:00 UTC
      const time_t a = utc_mk(y, 3, last_sunday(y, 3, 31), 1), b = utc_mk(y, 10, last_sunday(y, 10, 31), 1);
      return (t >= a && t < b) ? 60 : 0;
    }
    case 2: {  // New York: EDT from 2nd Sun Mar 07:00 UTC to 1st Sun Nov 06:00 UTC
      const time_t a = utc_mk(y, 3, nth_sunday(y, 3, 2), 7), b = utc_mk(y, 11, nth_sunday(y, 11, 1), 6);
      return (t >= a && t < b) ? -240 : -300;
    }
    default: return 540;  // Tokyo
  }
}
static const char *const k_city[4] = {"HERE", "LONDON", "NEW YORK", "TOKYO"};
void world_enter(uint32_t) {}
void world_leave(void) {}
void world_input(const UiIn &, uint32_t) {}
void world_draw(void) {
  gfx_fill(C_BG);
  w_status("WORLD", "");
  const time_t now = time(nullptr);
  const int here = world_offset_min(0, now);
  for (int i = 0; i < 4; i++) {
    const int y = 30 + i * 48;
    if (!gfx_vis(y, 44)) continue;
    w_card(10, y, 300, 42, 8, i == 0);
    const int off = world_offset_min((uint8_t)i, now);
    const time_t lt = now + off * 60;
    struct tm c;
    gmtime_r(&lt, &c);
    const bool day = c.tm_hour >= 6 && c.tm_hour < 18;
    if (day) gfx_circle(25, (float)y + 17, 5, SUN_C);
    else { gfx_circle(25, (float)y + 17, 5, RGB565C(150, 160, 230)); gfx_circle(28, (float)y + 15, 4.5f, i == 0 ? C_TILE_HI : C_TILE); }
    char nm[40];
    if (i == 0) snprintf(nm, sizeof(nm), "HERE  " AF_DOT "  %s", g_ui.d.loc.city[0] ? g_ui.d.loc.city : "IST");
    else snprintf(nm, sizeof(nm), "%s", k_city[i]);
    for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    gfx_micro(41, y + 10, nm, C_TXT);
    char o[16];
    const int dlt = i == 0 ? off : off - here;
    snprintf(o, sizeof(o), "%s%s%d:%02d", i == 0 ? "UTC" : "", dlt < 0 ? "-" : "+", abs(dlt) / 60, abs(dlt) % 60);
    gfx_micro(41, y + 25, o, C_DIM, GA_L, true);
    char tb[8];
    if (g_ui.tv) snprintf(tb, sizeof(tb), "%02d:%02d", c.tm_hour, c.tm_min);
    else snprintf(tb, sizeof(tb), "--:--");
    gfx_text_m(&AF_NUM_L, 302, y + 21, tb, C_TXT, GA_R);
  }
}

// ================================================================= System ==
static int8_t s_rssi[32];
static uint8_t s_rssi_n = 0;
static uint32_t s_rssi_t = 0;
static uint8_t s_sys_focus = 0;
static uint32_t s_reboot_arm = 0;
void system_enter(uint32_t) { s_sys_focus = 0; s_reboot_arm = 0; }
void system_leave(void) {}
void system_input(const UiIn &in, uint32_t now) {
  if (in.type == IN_ROT) { s_sys_focus = (uint8_t)((s_sys_focus + (in.d > 0 ? 1 : 2)) % 3); s_reboot_arm = 0; }
  else if (in.type == IN_CLICK || (in.type == IN_TAP && in.y >= 106)) {
    if (in.type == IN_TAP) { s_sys_focus = (uint8_t)ui_clampf((in.y - 106) / 36.0f, 0, 2); ui_consume(); }
    if (s_sys_focus == 0) settings_open_creds(false);
    else if (s_sys_focus == 1) ui_go(SCR_SETTINGS);
    else {
      if (s_reboot_arm && now - s_reboot_arm < 3000) { hal_log("[sys] restart requested from System app"); hal_reboot(); }
      else { s_reboot_arm = now; ui_toast("Press again to restart", 2500); }
    }
  }
}
void system_draw(void) {
  gfx_fill(C_BG);
  w_status("SYSTEM", "");
  const AuraData &d = g_ui.d;
  if (g_ui.now - s_rssi_t >= 2000) {  // sample (idempotent within a frame: time-gated)
    s_rssi_t = g_ui.now;
    if (s_rssi_n == 32) { memmove(s_rssi, s_rssi + 1, 31); s_rssi_n--; }
    s_rssi[s_rssi_n++] = d.net.wifi_state == WIFI_ST_UP ? d.net.rssi : -100;
  }
  w_card(10, 30, 146, 66, 8);
  w_card(164, 30, 146, 66, 8);
  if (gfx_vis(30, 66)) {
    char b[24];
    gfx_micro(20, 40, "WI-FI", C_DIM);
    if (d.net.wifi_state == WIFI_ST_UP) snprintf(b, sizeof(b), "%d dBm", (int)d.net.rssi);
    else snprintf(b, sizeof(b), "%s", d.net.wifi_state == WIFI_ST_NO_CREDS ? "Not set up" : "Offline");
    gfx_text_m(&AF_LABEL_M, 20, 61, b, C_TXT);
    if (s_rssi_n >= 2) {
      float xy[64];
      for (int i = 0; i < s_rssi_n; i++) {
        xy[2 * i] = 20 + i * 126.0f / 31;
        xy[2 * i + 1] = 88 - ui_clampf((s_rssi[i] + 90) / 50.0f, 0, 1) * 12;
      }
      gfx_polyline(xy, s_rssi_n, 1.2f, C_ACC);
    }
    gfx_micro(174, 40, "HEAP FREE", C_DIM);
    const uint32_t hf = hal_heap_free(), hl = hal_heap_largest();
    snprintf(b, sizeof(b), "%lu KB", (unsigned long)(hf / 1024u));
    gfx_text_m(&AF_LABEL_M, 174, 61, b, C_TXT);
    snprintf(b, sizeof(b), "MAX %luK", (unsigned long)(hl / 1024u));
    gfx_micro(300, 57, b, C_DIM, GA_R, true);
    gfx_rrect(174, 82, 126, 4, 2, C_TILE_HI);
    const int fw = (int)(126 * ui_clampf(hf / 180000.0f, 0, 1));
    gfx_rrect(174, 82, fw, 4, 2, hl >= 20480 ? C_UP : C_WARN);
  }
  static const char *const rows[3] = {"Credentials", "Settings", "Restart"};
  for (int i = 0; i < 3; i++) {
    const int y = 106 + i * 36;
    if (!gfx_vis(y, 32)) continue;
    const bool f = i == s_sys_focus;
    w_card(10, y, 300, 30, 8, f, f);
    if (f) gfx_rrect(10, y + 6, 3, 18, 1, C_ACC);
    gfx_text_m(&AF_ROW, 22, y + 15, rows[i], C_TXT);
    char v[24];
    if (i == 0) snprintf(v, sizeof(v), "%s", d.portal.on ? "PORTAL ON" : d.net.wifi_state == WIFI_ST_UP ? "CONNECTED" : "PRESS");
    else if (i == 1) snprintf(v, sizeof(v), "OPEN");
    else snprintf(v, sizeof(v), "%s", (s_reboot_arm && g_ui.now - s_reboot_arm < 3000) ? "PRESS AGAIN" : "PRESS TWICE");
    gfx_micro(300, y + 11, v, i == 2 && s_reboot_arm ? C_WARN : C_MUT, GA_R);
  }
  char f[64];
  const uint32_t up = hal_uptime_s();
  if (up >= 86400) snprintf(f, sizeof(f), "UPTIME %luD %02luH  " AF_DOT "  FW %s", (unsigned long)(up / 86400), (unsigned long)((up / 3600) % 24), AURA_VERSION);
  else snprintf(f, sizeof(f), "UPTIME %luH %02luM  " AF_DOT "  FW %s", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60), AURA_VERSION);
  w_footer(f);
}

// ================================================================= Oracle ==
// Provider "device": the question goes to xAI Grok or Groq (v3.1.2, resolved by
// net_task: manual selection, else xAI if its key is set, else Groq) through
// net_task. Provider "pc": the PC app relays it.
static uint8_t s_or_focus = 0;
static uint32_t s_or_sent = 0;     // pc provider: "needs PC" page
static uint32_t s_or_seq = 0;      // device provider: request being shown (0 = prompts)
static uint8_t s_or_last = 0;      // prompt index of s_or_seq (for retry)
static float s_tw = 0;             // typewriter: characters revealed
static uint32_t s_tw_t = 0;
static int s_or_scroll = 0;        // first wrapped line shown
static const char *const k_prompts[4] = {"Weather outfit?", "Fun fact", "Plan my day", "Joke"};
#define OR_CPS 80.0f               // typewriter speed, chars/s
#define OR_LH 15                   // reply line height
#define OR_LINES 11                // visible reply lines
static void or_wx(char *b, int n) {
  const WxData &w = g_ui.d.wx;
  if (!w.valid) { snprintf(b, n, "The weather is unknown right now."); return; }
  char hi[64] = "";
  if (w.ndays) snprintf(hi, sizeof(hi), " Today high %d C, low %d C, rain chance %u%%.", (w.day[0].hi_x10 + 5) / 10,
                        (w.day[0].lo_x10 + 5) / 10, (unsigned)w.day[0].pop);
  snprintf(b, n, "Weather in %s now: %d C (feels %d C), %s, humidity %u%%, wind %u km/h, UV %u.%u.%s",
           g_ui.d.loc.city[0] ? g_ui.d.loc.city : "my city", (w.temp_x10 + 5) / 10, (w.feels_x10 + 5) / 10,
           wx_text(w.code), (unsigned)w.hum, (unsigned)((w.wind_x10 + 5) / 10), (unsigned)(w.uv_x10 / 10),
           (unsigned)(w.uv_x10 % 10), hi);
}
static void or_send(uint8_t i) {
  char p[ORACLE_PROMPT_MAX], wx[200];
  switch (i) {
    case 0: or_wx(wx, sizeof(wx)); snprintf(p, sizeof(p), "%s What should I wear today? Be practical.", wx); break;
    case 1: snprintf(p, sizeof(p), "Tell me one surprising, true fun fact."); break;
    case 2: {
      or_wx(wx, sizeof(wx));
      static const char *const dn[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      if (g_ui.tv) snprintf(p, sizeof(p), "It is %s %02d:%02d. %s Suggest a simple plan for the rest of my day.",
                            dn[g_ui.lt.tm_wday % 7], g_ui.lt.tm_hour, g_ui.lt.tm_min, wx);
      else snprintf(p, sizeof(p), "%s Suggest a simple plan for my day.", wx);
      break;
    }
    default: snprintf(p, sizeof(p), "Tell me one short, clean, clever joke."); break;
  }
  s_or_last = i;
  s_or_seq = hal_oracle_ask(p);
  s_tw = 0;
  s_tw_t = g_ui.now;
  s_or_scroll = 0;
  hal_log("[oracle] ask prompt=%u seq=%lu", (unsigned)i, (unsigned long)s_or_seq);
}
static bool or_device(void) { return g_ui.d.oracle.provider == 0; }
static bool or_groq(void) { return g_ui.d.oracle.api == ORA_GROQ; }
static const OracleData *or_cur(void) { return g_ui.d.oracle.seq == s_or_seq ? &g_ui.d.oracle : nullptr; }
static uint32_t or_busy_left(const OracleData &o) {
  return (int32_t)(o.retry_until_ms - g_ui.now) > 0 ? (o.retry_until_ms - g_ui.now + 999u) / 1000u : 0;
}
// v3.1.4 Buddy v2: the Oracle tiles draw the shared app rig; its lids are painted in the
// tile's own fill (the local background), so both use OR_TILE_BG.
// v3.1.5 (Display UI ruling): OR_TILE_BG stays C_BG and is the ONE define for both the tile fill and
// the Buddy lid/cheek colour inside it. They must never drift apart: a lid colour that differs from
// the fill shows as a visible band over the eyes. Change this define only, never one use site.
#define OR_TILE_BG C_BG
static uint8_t s_or_bst = 0xFF;  // last Oracle state seen by the rig (0xFF none)
void oracle_enter(uint32_t now) {
  s_or_focus = 0; s_or_sent = 0; s_or_seq = 0;
  s_or_bst = 0xFF;
  buddy_rig_init(&g_rig, BRM_APP, now);
}
// Drives the rig from the request state: THINKING -> buddy_think_begin, STREAM -> speak,
// DONE / ERROR+KEYBAD / BUSY -> buddy_think_end(OK / ERR / BUSY).
void oracle_tick(uint32_t now) {
  const OracleData *op = or_cur();
  const uint8_t st = s_or_seq ? (op ? op->st : (uint8_t)OR_THINKING) : (uint8_t)OR_IDLE;
  if (st != s_or_bst) {
    BuddyRig *r = &g_rig;
    if (st == OR_THINKING) buddy_think_begin(r, now);
    else if (st == OR_STREAM) r->think = 0;
    else if (st == OR_DONE) buddy_think_end(r, now, BO_OK);
    else if (st == OR_ERROR || st == OR_KEYBAD) buddy_think_end(r, now, BO_ERR);
    else if (st == OR_BUSY) buddy_think_end(r, now, BO_BUSY);
    else if (s_or_bst != 0xFF && (r->think || r->speak)) buddy_think_end(r, now, BO_OK);
    s_or_bst = st;
  }
  // v3.1.5: speak only while Buddy is on screen. Oracle draws it in the prompts tile (no request) and the
  // thinking tile (queued / THINKING); the streaming reply view has no Buddy, so it never speaks there.
  const bool buddy_vis = !s_or_seq || st == OR_THINKING;
  buddy_speak(&g_rig, st == OR_STREAM && buddy_vis);
  buddy_place(&g_rig, s_or_seq ? 160.0f : 54.0f, s_or_seq ? 86.0f : 48.0f, s_or_seq ? 0.42f : 0.32f);
  buddy_tick(&g_rig, now);
}
void oracle_leave(void) { s_or_seq = 0; g_rig.think = 0; g_rig.speak = 0; }
void oracle_debug_view(uint32_t seq, float tw) { s_or_seq = seq; s_tw = tw; s_tw_t = g_ui.now; s_or_scroll = 0; }
void oracle_debug_focus(uint8_t) {}
void oracle_debug_draft(const char *) {}
void oracle_debug_keypad(uint8_t, uint8_t) {}
bool oracle_idle_locked(void) {
  if (!s_or_seq) return false;
  const OracleData *o = or_cur();
  const uint8_t st = o ? o->st : (uint8_t)OR_THINKING;
  return st == OR_THINKING || st == OR_STREAM;
}
void oracle_input(const UiIn &in, uint32_t now) {
  if (!g_ui.d.oracle.key_ok && or_device()) { if (in.type == IN_CLICK) settings_open_creds(false); return; }
  if (s_or_sent) {
    if (in.type == IN_CLICK || in.type == IN_TAP) { s_or_sent = 0; ui_consume(); }
    return;
  }
  if (s_or_seq) {
    const OracleData *o = or_cur();
    if (in.type == IN_ROT) { s_or_scroll += in.d > 0 ? 1 : -1; if (s_or_scroll < 0) s_or_scroll = 0; return; }
    if (in.type != IN_CLICK && in.type != IN_TAP) return;
    if (in.type == IN_TAP) ui_consume();
    if (o && o->st == OR_KEYBAD) { settings_open_creds(false); return; }
    if (o && o->st == OR_BUSY) {
      if (or_busy_left(*o)) { ui_toast(or_groq() ? "Groq is busy - wait for the countdown" : "Grok is busy - wait for the countdown", 1500); return; }
      or_send(s_or_last);
      return;
    }
    if (o && o->st == OR_ERROR) { or_send(s_or_last); return; }
    s_or_seq = 0;  // done / thinking: back to the prompts (a late reply is ignored)
    return;
  }
  if (in.type == IN_ROT) s_or_focus = (uint8_t)((s_or_focus + (in.d > 0 ? 1 : 5)) % 6);
  else if (in.type == IN_CLICK || (in.type == IN_TAP && in.y > 86)) {
    if (in.type == IN_TAP) {
      if (in.y < 166) s_or_focus = (uint8_t)((in.y >= 128 ? 2 : 0) + (in.x >= 160 ? 1 : 0));
      else s_or_focus = in.y < 196 ? 4 : 5;
      ui_consume();
    }
    if (s_or_focus < 4 && or_device()) or_send(s_or_focus);
    else s_or_sent = now;  // pc provider, or My prompts / History (PC app)
  }
}
// Word-wraps text[0..n) to width w; returns the line count, starts[] = line offsets.
static int or_wrap(const char *t, int n, int w, int16_t *starts, int maxl) {
  int lines = 0, i = 0;
  char b[96];
  while (i < n && lines < maxl) {
    while (i < n && t[i] == ' ') i++;
    if (i >= n) break;
    starts[lines++] = (int16_t)i;
    int j = i, last_sp = -1;
    for (;;) {
      if (j >= n || t[j] == '\n') break;
      const int l = j - i + 1 < (int)sizeof(b) - 1 ? j - i + 1 : (int)sizeof(b) - 2;
      memcpy(b, t + i, (size_t)l);
      b[l] = 0;
      if (gfx_text_w(&AF_BODY_S, b) > w) break;
      if (t[j] == ' ') last_sp = j;
      j++;
    }
    if (j < n && t[j] != '\n' && last_sp > i) j = last_sp;
    if (j == i) j = i + 1;
    i = j;
    if (i < n && t[i] == '\n') i++;
  }
  return lines;
}
static void or_reply_draw(const OracleData &o, int shown) {
  static int16_t st[48];
  static int s_wn = -1, s_wl = 0;
  if (shown != s_wn) { s_wn = shown; s_wl = or_wrap(o.text, shown, 288, st, 48); }
  // keep the typewriter cursor in view unless the user scrolled
  const int max_first = s_wl > OR_LINES ? s_wl - OR_LINES : 0;
  if (s_or_scroll > max_first) s_or_scroll = max_first;
  const int first = (shown < o.n && s_wl > OR_LINES) ? max_first : s_or_scroll;
  for (int k = 0; k < OR_LINES && first + k < s_wl; k++) {
    const int a = st[first + k];
    const int e = first + k + 1 < s_wl ? st[first + k + 1] : shown;
    const int y = 40 + k * OR_LH;
    if (!gfx_vis(y - 8, OR_LH)) continue;
    char b[96];
    int l = e - a;
    if (l > (int)sizeof(b) - 1) l = (int)sizeof(b) - 1;
    memcpy(b, o.text + a, (size_t)l);
    while (l > 0 && (b[l - 1] == ' ' || b[l - 1] == '\n')) l--;
    b[l] = 0;
    gfx_text_m(&AF_BODY_S, 16, y, b, C_TXT);
  }
  if (s_wl > OR_LINES) {  // scroll bar
    const int th = 160 * OR_LINES / s_wl, ty = 30 + (160 - th) * first / (max_first ? max_first : 1);
    gfx_rrect(312, 30, 3, 160, 1, blend565(C_BG, C_LINE, 150));
    gfx_rrect(311, ty, 5, th, 2, C_ACC);
  }
}
static void or_state_card(uint8_t glyph, uint16_t gc, const char *title, uint16_t tc, const char *l1, const char *l2) {
  w_card(10, 40, 300, 120, 10);
  if (!gfx_vis(40, 120)) return;
  gfx_a4(28, 76, 48, 48, state_glyph48(glyph), gc);
  w_text_ell(&AF_TITLE, 94, 80, title, 206, tc);
  if (l1) w_text_ell(&AF_BODY_S, 94, 104, l1, 206, C_MUT);
  if (l2) gfx_micro(94, 122, l2, C_DIM, GA_L, true);
}
static void or_view_draw(void) {
  const OracleData *op = or_cur();
  // Not picked up yet (flash): v3.1.3 a const placeholder (flash) instead of a 692 B .bss copy.
  static const OracleData k_queued{};  // zero, in flash; its st is overridden below
  const uint8_t think_api = op ? op->api : g_ui.d.oracle.api;
  const uint8_t st = op ? op->st : (uint8_t)OR_THINKING;
  if (!op) op = &k_queued;
  const OracleData &o = *op;
  char b[72];
  switch (st) {
    case OR_THINKING: {
      if (gfx_vis(60, 120)) {
        gfx_rrect(110, 54, 100, 70, 10, OR_TILE_BG);  // N01: theme background, not black
        gfx_clip(111, 55, 209, 123);
        buddy_render(&g_rig, 160, 86, 0.42f, OR_TILE_BG);  // v3.1.4: rig in THINK (oracle_tick)
        gfx_noclip();
        gfx_text_m(&AF_TITLE, 160, 146, "Thinking " AF_ELL, C_TXT, GA_C);
        const uint32_t el = (g_ui.now - s_tw_t) / 1000u;
        snprintf(b, sizeof(b), "%s " AF_DOT " %lu S (TIMEOUT 20 S)", ora_api_name(think_api), (unsigned long)el);
        gfx_micro(160, 164, b, C_MUT, GA_C, true);
        w_spinner(160, 190, 8, C_ACC, C_BG);
      }
      w_footer("PRESS: BACK TO PROMPTS");
      return;
    }
    case OR_STREAM: case OR_DONE: {
      const float dt = (float)(g_ui.now - s_tw_t) / 1000.0f;
      s_tw_t = g_ui.now;
      if (s_tw < o.n) s_tw += dt * OR_CPS * (o.n - s_tw > 200 ? 2.0f : 1.0f);
      if (s_tw > o.n) s_tw = o.n;
      const int shown = (int)s_tw;
      or_reply_draw(o, shown);
      if (shown < o.n || o.st == OR_STREAM) {
        w_footer(o.api == ORA_GROQ ? "GROQ IS ANSWERING " AF_ELL "   PRESS: BACK" : "GROK IS ANSWERING " AF_ELL "   PRESS: BACK");
      } else {
        snprintf(b, sizeof(b), "%s%s" AF_DOT " PRESS: NEW QUESTION", o.reason[0] ? o.reason : o.model, o.reason[0] ? " " : "  ");
        w_footer(b);
      }
      return;
    }
    case OR_BUSY: {
      const uint32_t left = or_busy_left(o);
      snprintf(b, sizeof(b), left ? "Try again in %lu s" : "Ready to retry", (unsigned long)left);
      or_state_card(SG_MONITOR, C_WARN, o.api == ORA_GROQ ? "Groq is busy" : "Grok is busy", C_WARN, b, "RATE LIMIT (HTTP 429)");
      w_footer(left ? "WAIT FOR THE COUNTDOWN   HOLD: BACK" : "PRESS: RETRY   HOLD: BACK");
      return;
    }
    case OR_KEYBAD:
      or_state_card(SG_KEY, C_DN, "Key rejected", C_DN,
                    "Check this key in Credentials", o.reason);
      w_footer("PRESS: CREDENTIALS   HOLD: BACK");
      return;
    default:
      or_state_card(SG_MONITOR, C_DN, "No answer", C_TXT, o.reason[0] ? o.reason : "Unknown error",
                    o.http ? (o.api == ORA_GROQ ? "FROM API.GROQ.COM" : "FROM API.X.AI") : nullptr);
      w_footer("PRESS: RETRY   HOLD: BACK");
      return;
  }
}
void oracle_draw(void) {
  gfx_fill(C_BG);
  w_status("ORACLE AI", "");
  const bool dev = or_device();
  if (dev && !g_ui.d.oracle.key_ok) {
    st_missing_key(g_ui.d.oracle.api_sel == ORA_SEL_AUTO ? "XAI OR GROQ API" : or_groq() ? "GROQ API" : "XAI GROK API");
    w_footer("PRESS: CREDENTIALS   HOLD: BACK");
    return;
  }
  if (s_or_sent) {
    st_needs_pc(s_or_focus < 4 ? "Answers are relayed by the PC app" : "Prompts and history sync from the PC app");
    w_footer("PRESS: BACK TO PROMPTS");
    return;
  }
  if (s_or_seq) { or_view_draw(); return; }
  if (gfx_vis(28, 54)) {
    gfx_rrect(8, 28, 92, 52, 6, OR_TILE_BG);  // N01
    gfx_rrect_frame(8, 28, 92, 52, 6, C_LINE);
    gfx_clip(9, 29, 99, 79);
    buddy_render(&g_rig, 54, 48, 0.32f, OR_TILE_BG);
    gfx_noclip();
    gfx_text_m(&AF_TITLE, 112, 39, "Ask me anything", C_TXT);
    gfx_micro(112, 54, "PICK A PROMPT " AF_DOT " PRESS TO SEND", C_MUT, GA_L, true);
    if (dev) {
      char b[48];
      snprintf(b, sizeof(b), "%s " AF_DOT " %s", ora_api_name(g_ui.d.oracle.api),
               g_ui.d.oracle.model[0] ? g_ui.d.oracle.model : (or_groq() ? "LLAMA-3.1-8B-INSTANT" : "GROK-3-MINI"));
      for (char *q = b; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
      gfx_micro(112, 67, b, C_MUT, GA_L, true);
    } else gfx_micro(112, 67, "VIA THE AURA PC APP", C_MUT, GA_L, true);
  }
  for (int i = 0; i < 4; i++) {
    const int x = (i & 1) ? 162 : 8, y = (i & 2) ? 128 : 88, w = (i & 1) ? 150 : 152;
    if (!gfx_vis(y, 34)) continue;
    const bool f = i == s_or_focus;
    w_card(x, y, w, 34, 8, f, f);  // Q03: standard focus style (C_TILE_HI + C_FOCUS_BORDER)
    gfx_text_m(&AF_ROW, x + w / 2, y + 17, k_prompts[i], C_TXT, GA_C);
  }
  static const char *const rn[2] = {"My prompts", "History"};
  static const char *const rs[2] = {"EDITED FROM PC APP", "LAST 5 ANSWERS"};
  for (int i = 0; i < 2; i++) {
    const int y = 170 + i * 28;
    if (!gfx_vis(y, 26)) continue;
    const bool f = s_or_focus == 4 + i;
    w_card(8, y, 304, 24, 8, f, f);
    const int nx = gfx_text_m(&AF_ROW, 20, y + 12, rn[i], C_TXT);
    gfx_micro(nx + gfx_text_w(&AF_ROW, rn[i]) + 10, y + 8, rs[i], C_DIM, GA_L, true);
    const int cw = gfx_text_w(&AF_LABEL, AF_CHEV);  // N06: chevron from AF_LABEL (the micro one is a 1 px tick)
    gfx_text_m(&AF_LABEL, 300, y + 12, AF_CHEV, C_MUT, GA_R);
    gfx_micro(300 - cw - 4, y + 8, "PC", C_MUT, GA_R, true);
  }
  w_footer("TURN: MOVE   PRESS: SEND   HOLD: BACK");
}

// ================================================================== Macro ==
struct MKey { const char *lbl; uint16_t usage; };
static const MKey k_keys[6] = {{"PREV", 0x00B6}, {"PLAY", 0x00CD}, {"NEXT", 0x00B5},
                               {"VOL -", 0x00EA}, {"MUTE", 0x00E2}, {"VOL +", 0x00E9}};
static uint8_t s_mk_focus = 1;
static uint32_t s_mk_flash = 0;
static uint8_t s_mk_last = 255;
void macro_debug_flash(uint8_t, uint8_t key) {
  if (key < 6) { s_mk_focus = key; s_mk_last = key; }
  s_mk_flash = g_ui.now;
}
void macro_enter(uint32_t) {
  s_mk_focus = 1;
  if (g_ui.prefs.pc_keys) hal_ble_kick();  // (re)start advertising for a 2-min window
}
void macro_leave(void) {}
static void mk_send(uint8_t i, uint32_t now, uint8_t n) {
  s_mk_last = i;
  s_mk_flash = now;
  if (g_ui.d.ble.connected) hal_hid_consumer(k_keys[i].usage, n);
  else ui_toast("No PC connected", 1500);
}
void macro_input(const UiIn &in, uint32_t now) {
  if (!g_ui.prefs.pc_keys) {
    if (in.type == IN_CLICK || (in.type == IN_TAP && in.y > 140)) {
      g_ui.prefs.pc_keys = 1;
      ui_prefs_changed();
      hal_ble_enable(true);
      hal_ble_kick();
      ui_toast("PC keys on", 1500);
      ui_consume();
    }
    return;
  }
  const bool conn = g_ui.d.ble.connected;
  switch (in.type) {
    case IN_ROT:
      if (conn) mk_send(in.d > 0 ? 5 : 3, now, (uint8_t)(in.d > 0 ? in.d : -in.d));
      break;
    case IN_CLICK:
      if (!conn && !g_ui.d.ble.started) { hal_ble_kick(); ui_toast("Advertising for 2 min", 1500); }
      else mk_send(1, now, 1);
      break;
    case IN_TAP: {
      if (in.y >= 108 && in.y < 212) {
        const int col = in.x < 112 ? 0 : (in.x < 212 ? 1 : 2), row = in.y < 160 ? 0 : 1;
        s_mk_focus = (uint8_t)(row * 3 + col);
        mk_send(s_mk_focus, now, 1);
        ui_consume();
      } else if (in.y >= 30 && in.y < 100 && !g_ui.d.ble.started) { hal_ble_kick(); ui_consume(); }
      break;
    }
    default: break;
  }
}
void macro_draw(void) {
  gfx_fill(C_BG);
  w_status("MACRO", "");
  const BleData &b = g_ui.d.ble;
  if (!g_ui.prefs.pc_keys) {
    w_card(30, 48, 260, 142);
    gfx_a4(136, 64, 48, 48, state_glyph48(SG_KEY), C_DIM);  // keyboard state glyph
    gfx_text_m(&AF_ROW_FOCUS, 160, 130, "PC keys off", C_TXT, GA_C);
    gfx_text_m(&AF_BODY_S, 160, 150, "Bluetooth stays off to save memory", C_MUT, GA_C);
    const int w = gfx_micro_w("PRESS: TURN ON", true) + 12;
    w_chip(160 - w / 2, 164, "PRESS: TURN ON", true, C_UP);
    w_footer("SETTINGS " AF_CHEV " PC KEYS (BLUETOOTH)");
    return;
  }
  // link card
  w_card(10, 30, 300, 68, 10, b.connected != 0);
  if (gfx_vis(30, 68)) {
    const uint16_t lc = b.connected ? C_UP : (b.started ? C_ACC : C_DIM);
    gfx_a4(22, 52, 24, 24, key_glyph24(KG_BT), lc);
    const char *t, *s2;
    char sb[48];
    if (b.connected) { t = "Connected to PC"; s2 = "TURN: VOLUME  " AF_DOT "  PRESS: PLAY / PAUSE"; }
    else if (b.started) {
      t = "Pairing as \"" BLE_DEVICE_NAME "\"";
      snprintf(sb, sizeof(sb), "STOPS IN %lu:%02lu IF NO PC CONNECTS", (unsigned long)(b.adv_left_ms / 60000u), (unsigned long)((b.adv_left_ms / 1000u) % 60u));
      s2 = sb;
      w_spinner(292, 64, 7, C_ACC, C_TILE);
    } else { t = "Bluetooth idle"; s2 = "PRESS TO ADVERTISE FOR 2 MIN"; }
    w_text_ell(&AF_ROW_FOCUS, 56, 56, t, b.started && !b.connected ? 216 : 240, C_TXT);
    gfx_micro(56, 72, s2, b.connected ? C_UP : C_MUT, GA_L, true);
  }
  for (int i = 0; i < 6; i++) {
    const int x = 10 + (i % 3) * 101, y = 108 + (i / 3) * 52;
    if (!gfx_vis(y, 48)) continue;
    const bool flash = i == s_mk_last && g_ui.now - s_mk_flash < 220;
    w_card(x, y, 96, 46, 8, flash, flash);
    const uint16_t ic = b.connected ? (flash ? C_ACC : C_TXT) : C_DIM;
    gfx_a4(x + 36, y + 5, 24, 24, key_glyph24((uint8_t)i), ic);  // KG_PREV..KG_VOLUP match k_keys order
    gfx_micro(x + 48, y + 31, k_keys[i].lbl, b.connected ? C_MUT : C_DIM, GA_C, true);
  }
  char f[48];
  snprintf(f, sizeof(f), "SENT %lu  " AF_DOT "  TAP A KEY  " AF_DOT "  HOLD: BACK", (unsigned long)b.hid_sent);
  w_footer(f);
}

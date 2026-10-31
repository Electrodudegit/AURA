// ui_core.cpp - screen manager, navigation, idle/night logic and shared widgets.
#include "prof.h"
#include "ui_core.h"
#include "apps.h"
#include "config.h"
#include "tz.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

UiCtx g_ui;
static bool s_consumed = false;
static char s_toast[48];
static uint32_t s_toast_until = 0;
static uint8_t s_bl_last = 255;

#define IDLE_BUDDY_MS   45000u
#define IDLE_SAVER_MS   (10u * 60u * 1000u)
#define IDLE_BL_DIM_MS  20000u                 // dim the panel before Buddy takes over
#define IDLE_BL_OFF_MS  (5u * 60u * 1000u)     // after the saver has been up, panel off

static const Screen *const k_screens[SCR_COUNT] = {&SCR_BOOT_DEF, &SCR_HOME_DEF, &SCR_MENU_DEF, &SCR_APP_DEF,
                                                   &SCR_SETTINGS_DEF, &SCR_BUDDY_DEF, &SCR_SAVER_DEF,
                                                   &SCR_CAL_DEF, &SCR_TGRID_DEF};

// ------------------------------------------------------------------- apps --
#define APPROW(id, nm, ti, ic, sub, fn) {nm, ti, ic, sub, fn##_enter, fn##_leave, fn##_input, fn##_draw}
static const AppDef k_apps[APP_COUNT] = {
  {"Home", "HOME", IC_HOME, "Split-flap clock", nullptr, nullptr, nullptr, nullptr},
  APPROW(APP_WEATHER, "Weather", "WEATHER", IC_WEATHER, "Now and today", weather),
  APPROW(APP_FORECAST, "Forecast", "FORECAST", IC_FORECAST, "Next 5 days", forecast),
  APPROW(APP_SKY, "Sky", "SKY", IC_SKY, "Sun arc and moon", sky),
  APPROW(APP_AIR, "Air", "AIR", IC_AIR, "US AQI, PM2.5, PM10", air),
  APPROW(APP_CRYPTO, "Crypto", "CRYPTO", IC_CRYPTO, "CoinGecko prices", crypto),
  APPROW(APP_STOCKS, "Stocks", "STOCKS", IC_STOCKS, "Twelve Data quotes", stocks),
  APPROW(APP_NEWS, "News", "NEWS", IC_NEWS, "Top stories (RSS)", news),
  APPROW(APP_CRICKET, "Cricket", "CRICKET", IC_CRICKET, "Live scores", cricket),
  APPROW(APP_FOCUS, "Focus", "FOCUS", IC_FOCUS, "Pomodoro timer", focus),
  APPROW(APP_WORLD, "World", "WORLD", IC_WORLD, "World clocks", world),
  APPROW(APP_SYSTEM, "System", "SYSTEM", IC_SYSTEM, "Wi-Fi, heap, version", system),
  APPROW(APP_ORACLE, "Oracle", "ORACLE AI", IC_ORACLE, "Ask me anything", oracle),
  APPROW(APP_BUDDY, "Buddy", "BUDDY", IC_BUDDY, "Your desk pet", buddyapp),
  APPROW(APP_MACRO, "Macro", "MACRO", IC_MACRO, "Bluetooth media keys", macro),
  APPROW(APP_PC, "PC Status", "PC STATUS", IC_PCSTATUS, "CPU, RAM, alerts", pc),
  {"Settings", "SETTINGS", IC_SETTINGS, "Wi-Fi, menu, touch", nullptr, nullptr, nullptr, nullptr},
};
const AppDef *app_def(uint8_t id) { return id < APP_COUNT ? &k_apps[id] : nullptr; }

void app_value(uint8_t id, char *b, int n, uint16_t *col) {
  const AuraData &d = g_ui.d;
  *col = C_MUT;
  b[0] = 0;
  switch (id) {
    case APP_HOME:
      if (g_ui.tv) snprintf(b, n, "%02d:%02d", g_ui.lt.tm_hour, g_ui.lt.tm_min);
      else snprintf(b, n, "--:--");
      break;
    case APP_WEATHER:
      if (d.wx.valid) fmt_temp(b, n, d.wx.temp_x10);
      else snprintf(b, n, "--");
      break;
    case APP_FORECAST:
      if (d.wx.valid && d.wx.ndays)
        snprintf(b, n, "H%d L%d", (d.wx.day[0].hi_x10 + 5) / 10, (d.wx.day[0].lo_x10 + 5) / 10);
      else snprintf(b, n, "--");
      break;
    case APP_SKY: {
      if (!d.wx.valid || d.wx.sunset_min < 0) { snprintf(b, n, "--"); break; }
      const int nowm = g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min;
      char t[8];
      if (nowm < d.wx.sunset_min && nowm >= d.wx.sunrise_min) { fmt_hm(t, sizeof(t), d.wx.sunset_min); snprintf(b, n, "SET %s", t); }
      else { fmt_hm(t, sizeof(t), d.wx.sunrise_min); snprintf(b, n, "RISE %s", t); }
      break;
    }
    case APP_AIR:
      if (d.air.valid) {
        snprintf(b, n, "AQI %u", (unsigned)d.air.aqi);
        int i = 0;
        while (i < 5 && d.air.aqi > AQI_BREAKS[i + 1]) i++;
        *col = AQI_COL[i];
      } else snprintf(b, n, "--");
      break;
    case APP_CRYPTO:
      if (d.crypto.valid && d.crypto.n) {
        const float c = d.crypto.c[0].chg;
        snprintf(b, n, "%s %s%.1f%%", d.crypto.c[0].sym, c >= 0 ? "+" : "", (double)c);
        *col = c >= 0 ? C_UP : C_DN;
      } else snprintf(b, n, "--");
      break;
    case APP_STOCKS:
      if (!d.keys.stocks) { snprintf(b, n, "ADD KEY"); *col = C_DIM; }
      else if (d.stocks.valid && d.stocks.n) {
        const float c = d.stocks.row[0].chg;
        snprintf(b, n, "%s %s%.1f%%", d.stocks.row[0].sym, c >= 0 ? "+" : "", (double)c);
        *col = c >= 0 ? C_UP : C_DN;
      } else snprintf(b, n, "--");
      break;
    case APP_CRICKET:
      if (!d.keys.cricket) { snprintf(b, n, "ADD KEY"); *col = C_DIM; }
      else if (d.cricket.valid && d.cricket.n) {
        const CricMatch &m = d.cricket.m[0];
        if (m.started && !m.ended) {
          if (m.ni) snprintf(b, n, "%d/%d", (int)m.inn[m.ni - 1].r, (int)m.inn[m.ni - 1].w);
          else snprintf(b, n, "LIVE");
          *col = C_UP;
        } else snprintf(b, n, "%s", m.ended ? "ENDED" : "SOON");
      } else snprintf(b, n, "--");
      break;
    case APP_NEWS:
      if (d.news.valid) snprintf(b, n, "%u STORIES", (unsigned)d.news.n);
      else snprintf(b, n, "--");
      break;
    case APP_FOCUS:
      if (g_focus.st == 1 || g_focus.st == 2) {
        const uint32_t left = g_focus.st == 1 ? (g_focus.t_end > g_ui.now ? g_focus.t_end - g_ui.now : 0) : g_focus.t_left;
        snprintf(b, n, "%02lu:%02lu", (unsigned long)(left / 60000u), (unsigned long)((left / 1000u) % 60u));
        *col = C_ACC;
      } else snprintf(b, n, "%u MIN", (unsigned)g_ui.prefs.focus_min);
      break;
    case APP_WORLD: {
      if (!g_ui.tv) { snprintf(b, n, "LDN --:--"); break; }
      const time_t t = time(nullptr) + world_offset_min(1, time(nullptr)) * 60;
      struct tm u;
      gmtime_r(&t, &u);
      snprintf(b, n, "LDN %02d:%02d", u.tm_hour, u.tm_min);
      break;
    }
    case APP_SYSTEM: snprintf(b, n, "HEAP %luK", (unsigned long)(hal_heap_free() / 1024u)); break;
    case APP_ORACLE: {  // v3.1.2: key of the resolved API (xAI or Groq); PC provider needs no key
      const bool ok = d.oracle.provider || d.oracle.key_ok;
      snprintf(b, n, "%s", ok ? "ASK" : "ADD KEY");
      *col = ok ? C_MUT : C_DIM;
      break;
    }
    case APP_BUDDY: snprintf(b, n, "SAY HI"); break;
    case APP_MACRO:
      if (!g_ui.prefs.pc_keys) { snprintf(b, n, "KEYS OFF"); *col = C_DIM; }
      else if (d.ble.connected) { snprintf(b, n, "CONNECTED"); *col = C_UP; }
      else if (d.ble.started) snprintf(b, n, "PAIRING");
      else snprintf(b, n, "BLE IDLE");
      break;
    case APP_SETTINGS:
      snprintf(b, n, "%s", d.net.wifi_state == WIFI_ST_NO_CREDS ? "SET UP WI-FI" : "");
      if (d.net.wifi_state == WIFI_ST_NO_CREDS) *col = C_WARN;
      break;
    case APP_PC:
      if (!g_ui.prefs.comp_on) { snprintf(b, n, "NOT LINKED"); *col = C_DIM; }  // HMI sheet tile values
      else if (d.comp.live) { snprintf(b, n, "CPU %u%%", (unsigned)d.comp.cpu); *col = C_MUT; }
      else if (d.comp.paired) snprintf(b, n, "PAIRED");
      else { snprintf(b, n, "PAIR"); *col = C_WARN; }
      break;
    default: break;
  }
}

// ------------------------------------------------------------- navigation --
void ui_consume(void) { s_consumed = true; }
uint32_t ui_elapsed(void) { return g_ui.now - g_ui.scr_t0; }
const char *ui_screen_name(void) {
  if (g_ui.scr == SCR_APP) return k_apps[g_ui.app].title;
  return k_screens[g_ui.scr]->name;
}
bool ui_wants_raw_touch(void) { return k_screens[g_ui.scr]->raw_touch; }

void ui_go(uint8_t scr) {
  if (scr >= SCR_COUNT) return;
  const Screen *cur = k_screens[g_ui.scr];
  if (cur->leave) cur->leave();
  if (scr != SCR_BUDDY && scr != SCR_SAVER) g_ui.buddy_idle = false;
  g_ui.scr = scr;
  g_ui.scr_t0 = g_ui.now;
  hal_log("[ui] screen -> %s", scr == SCR_APP ? k_apps[g_ui.app].title : k_screens[scr]->name);
  if (k_screens[scr]->enter) k_screens[scr]->enter(g_ui.now);
}
static uint8_t app_job(uint8_t app);
void ui_open_app(uint8_t app) {
  if (app >= APP_COUNT) return;
  if (app == APP_HOME) { ui_go(SCR_HOME); return; }
  if (app == APP_SETTINGS) { ui_go(SCR_SETTINGS); return; }
  // Home caches sit on the internal heap until the next frame. Drop them before
  // the app's enter() so a feed that was skipped for RAM can run now.
  if (home_cache_bytes()) home_release_caches();
  const uint8_t j = app_job(app);
  if (j != 0xFF && g_ui.d.job[j].st == JS_HEAP) {
    hal_net_refresh((int)j);
    ui_toast("Freed caches, retrying", 1400);
  }
  if (g_ui.scr == SCR_APP) {  // app -> app: leave the old app first
    const Screen *cur = k_screens[SCR_APP];
    if (cur->leave) cur->leave();
    g_ui.scr = SCR_MENU;
  }
  g_ui.app_from = g_ui.scr;
  g_ui.app = app;
  ui_go(SCR_APP);
}
void ui_back(void) {
  switch (g_ui.scr) {
    case SCR_APP: ui_go(g_ui.app_from == SCR_HOME ? SCR_HOME : SCR_MENU); break;
    case SCR_SETTINGS: ui_go(SCR_MENU); break;
    case SCR_CAL: case SCR_TGRID: ui_go(SCR_SETTINGS); break;
    case SCR_MENU: case SCR_BUDDY: case SCR_SAVER: ui_go(SCR_HOME); break;
    default: break;
  }
}
void ui_toast(const char *msg, uint32_t ms) {
  snprintf(s_toast, sizeof(s_toast), "%s", msg);
  s_toast_until = g_ui.now + ms;
}
void ui_prefs_changed(void) { hal_prefs_save(&g_ui.prefs); }

void ui_init(uint32_t now) {
  memset(&g_ui, 0, sizeof(g_ui));
  g_ui.now = now;
  g_ui.last_input = now;
  g_ui.prefs.bright = BL_BRIGHT_DEF;
  g_ui.prefs.focus_min = 25;
  g_ui.prefs.pc_keys = 1;
  g_ui.prefs.last_app = APP_HOME;
  hal_prefs_load(&g_ui.prefs);
  if (g_ui.prefs.face > 3) g_ui.prefs.face = 0;
  if (g_ui.prefs.bright < 10 || g_ui.prefs.bright > 100) g_ui.prefs.bright = BL_BRIGHT_DEF;
  if (g_ui.prefs.focus_min != 15 && g_ui.prefs.focus_min != 25 && g_ui.prefs.focus_min != 50) g_ui.prefs.focus_min = 25;
  if (g_ui.prefs.last_app >= APP_COUNT) g_ui.prefs.last_app = APP_HOME;
  if (g_ui.prefs.tz >= tz_count()) g_ui.prefs.tz = 0;
  if (g_ui.prefs.macro_layer > 2) g_ui.prefs.macro_layer = 0;
  g_ui.prefs.comp_on = g_ui.prefs.comp_on ? 1 : 0;
  hal_tz_apply(g_ui.prefs.tz);
  hal_comp_enable(g_ui.prefs.comp_on != 0);
  // BLE policy: PC keys ON -> advertise at boot for a 2-min no-connect window.
  hal_ble_enable(g_ui.prefs.pc_keys != 0);
  if (g_ui.prefs.pc_keys) hal_ble_kick();
  g_ui.scr = SCR_BOOT;
  g_ui.scr_t0 = now;
  if (SCR_BOOT_DEF.enter) SCR_BOOT_DEF.enter(now);
}

void ui_frame_begin(uint32_t now, const AuraData *d, const struct tm *lt, bool tv) {
  g_ui.now = now;
  g_ui.frame++;
  if (d) g_ui.d = *d;
  if (lt) g_ui.lt = *lt;
  g_ui.tv = tv;
  const int m = g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min;
  g_ui.night = tv && (m >= 22 * 60 + 30 || m < 6 * 60 + 30);
  focus_tick(now);
  const Screen *s = k_screens[g_ui.scr];
  if (s->tick) s->tick(now);
  // idle takeover: Buddy after 45 s, split-flap saver after a further 10 min.
  // A screen hold (portal page, running Focus timer, Oracle think/stream, Macro while
  // linked) must not call leave(): Settings leave stops the portal via hal_portal(false).
  const bool hold = s->idle_lock && s->idle_lock();
  if (hold) g_ui.last_input = now;
  const uint32_t idle = now - g_ui.last_input;
  if (!hold && s->idle_buddy && idle >= IDLE_BUDDY_MS) {
    ui_go(SCR_BUDDY);
    g_ui.buddy_idle = true;
  } else if (g_ui.scr == SCR_BUDDY && g_ui.buddy_idle && idle >= IDLE_BUDDY_MS + IDLE_SAVER_MS) {
    ui_go(SCR_SAVER);
    g_ui.buddy_idle = true;
  }
  // Backlight. Idle dims, then the saver dims further, then the panel goes dark.
  // Night dim still applies on the idle Buddy / saver and can go lower. A hold
  // (portal, running Focus, Oracle think/stream, Macro while linked) refreshes
  // last_input above, so those screens stay at the user's brightness.
  uint32_t bl = g_ui.prefs.bright;
  if (g_ui.scr != SCR_BOOT) {
    const uint32_t deep = IDLE_BUDDY_MS + IDLE_SAVER_MS;
    if (idle >= deep + IDLE_BL_OFF_MS) bl = 0;
    else if (idle >= deep) {
      bl = bl * BL_SAVER_PCT / 100u;
      if (bl < 4) bl = 4;
    } else if (idle >= IDLE_BL_DIM_MS) {
      bl = bl * BL_IDLE_PCT / 100u;
      if (bl < 8) bl = 8;
    }
  }
  // Night dim is relative to the current idle level (Buddy and saver both get it).
  if (bl && g_ui.prefs.night_dim && g_ui.night && g_ui.buddy_idle) {
    bl = bl * BL_NIGHT_PCT / 100u;
    if (bl < 4) bl = 4;
  }
  if (bl != s_bl_last) { s_bl_last = (uint8_t)bl; hal_backlight((uint8_t)bl); }
}
// Settings brightness step: push prefs.bright to the PWM now (the user just pressed,
// so no idle / night factor applies) instead of waiting for the next ui_frame_begin.
void ui_backlight_refresh(void) {
  s_bl_last = g_ui.prefs.bright;
  hal_backlight(g_ui.prefs.bright);
}

bool ui_is_back(const UiIn &in) {
  // 44 px square (UI_MIN_HIT_PX). The glyph itself sits in the 24 px status
  // band; the target extends into the corner under it. App screens do not draw
  // a chevron there (it would cover the 16 px status icon, including Oracle).
  if (in.type == IN_HOLD) return true;
  if (in.type == IN_SWR && in.x0 < UI_MIN_HIT_PX) return true;
  if (in.type == IN_TAP && in.x < UI_MIN_HIT_PX && in.y < UI_MIN_HIT_PX) return true;
  return false;
}

uint32_t ui_frame_period_ms(void) {
  // Buddy blinks are ~60 ms, so any screen that draws the face stays at 25 fps.
  // Home, menu and Buddy already skip unchanged bands. Other static screens
  // drop the frame rate instead of growing a new key (a key must match every pixel).
  if (g_ui.scr == SCR_BOOT || g_ui.scr == SCR_CAL || g_ui.scr == SCR_TGRID) return UI_FRAME_MS;
  if ((int32_t)(s_toast_until - g_ui.now) > 0 && s_toast[0]) return UI_FRAME_MS;
  if (buddy_on_screen()) return UI_FRAME_MS;
  if (g_ui.scr == SCR_APP && g_ui.app == APP_ORACLE && oracle_idle_locked()) return UI_FRAME_MS;
  const uint32_t idle = g_ui.now - g_ui.last_input;
  if (idle < 2500u) return UI_FRAME_MS;
  if (g_ui.scr == SCR_APP && g_ui.app == APP_ORACLE) return 80;  // prompts tile still blinks
  if (idle < 12000u) return 100;                                 // 10 fps
  return 250;                                                    // 4 fps
}

bool ui_light_sleep_ok(void) {
  // Light sleep halts every task, including TLS and BLE. Only when the Wi-Fi
  // driver is down (no credentials), BLE is not on the air, and nobody is
  // pairing or touching the panel. Modem sleep (WiFi.setSleep) still applies
  // whenever Wi-Fi is up; that path does not enter light sleep.
  if (g_ui.d.net.wifi_state != WIFI_ST_NO_CREDS) return false;
  if (g_ui.d.ble.started || g_ui.d.ble.connected || g_ui.d.ble.advertising) return false;
  if (g_ui.d.portal.on || g_ui.d.comp.on) return false;
  if (g_ui.scr == SCR_BOOT || g_ui.scr == SCR_CAL || g_ui.scr == SCR_TGRID) return false;
  if (g_ui.now - g_ui.last_input < 8000u) return false;
  return true;
}

void ui_input(const UiIn &in) {
  g_ui.last_input = g_ui.now;
  if (g_ui.scr == SCR_BOOT) {  // tap, click or knob turn skips the rest of the sequence
    const Screen *b = k_screens[SCR_BOOT];
    if (b->input) b->input(in, g_ui.now);
    return;
  }
  if (g_ui.scr == SCR_SAVER) { ui_go(SCR_HOME); return; }  // any touch / knob wakes
  s_consumed = false;
  const Screen *s = k_screens[g_ui.scr];
  if (s->input) s->input(in, g_ui.now);
  if (s_consumed || g_ui.scr == SCR_HOME) return;
  if (ui_is_back(in)) ui_back();
}
void ui_touch_raw(const UiTouchRaw &r) {
  g_ui.raw = r;
  if (r.down) g_ui.last_input = g_ui.now;
  if (g_ui.scr == SCR_CAL) cal_on_raw(r, g_ui.now);
}
uint32_t ui_hash(uint32_t h, uint32_t v) {
  h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
  return h;
}
uint32_t ui_hash_str(uint32_t h, const char *s) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return ui_hash(h, 0x5A);
}
static int status_wifi_lvl(void) {
  const AuraData &d = g_ui.d;
  if (d.net.wifi_state != WIFI_ST_UP) return 0;
  return d.net.rssi > -60 ? 3 : (d.net.rssi > -72 ? 2 : 1);
}
uint32_t w_status_key(bool home) {
  const AuraData &d = g_ui.d;
  uint32_t h = ui_hash(0x51A7u, home ? 1u : 2u);
  h = ui_hash(h, (uint32_t)(int32_t)d.batt.pct);
  h = ui_hash(h, (uint32_t)status_wifi_lvl());
  h = ui_hash(h, d.ble.connected ? 1u : 0u);
  return h;
}
uint32_t ui_band_key(int band, BandWin *w) {
  w->x0 = w->x1 = 0;
  w->key = 0;
  const Screen *s = k_screens[g_ui.scr];
  if (!s->band_key) return 0;
  if ((int32_t)(s_toast_until - g_ui.now) > 0 && s_toast[0]) return 0;  // toast: full frames
  const uint32_t k = s->band_key(band, w);
  if (w->x0 < 0) w->x0 = 0;
  if (w->x1 > GFX_W) w->x1 = GFX_W;
  if (w->x1 <= w->x0) { w->x0 = w->x1 = 0; w->key = 0; }
  if (!k) return 0;
  const uint32_t r = ui_hash(ui_hash(k, (uint32_t)g_ui.scr), (uint32_t)band);
  return r ? r : 1;
}

void ui_draw(void) {
  const Screen *s = k_screens[g_ui.scr];
  if (s->draw) s->draw();
  gfx_origin(0, 0);
  gfx_noclip();
  w_toast_draw();
}

// ---------------------------------------------------------------- helpers --
float ease_out(float u) {
  u = ui_clampf(u, 0, 1);
  const float iu = 1.0f - u;
  return 1.0f - iu * iu * iu;
}
float ui_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
void fmt_temp(char *b, int n, int16_t x10) {
  const int v = x10 >= 0 ? (x10 + 5) / 10 : -((-x10 + 5) / 10);
  snprintf(b, n, "%d" AF_DEG, v);
}
void fmt_hm(char *b, int n, int minutes) {
  if (minutes < 0) { snprintf(b, n, "--:--"); return; }
  snprintf(b, n, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
}
uint32_t job_age_min(uint8_t job) {
  const JobInfo &j = g_ui.d.job[job];
  if (!j.ok_ms) return 0xFFFFFFFFu;
  return (g_ui.now - j.ok_ms) / 60000u;
}
void fmt_age(char *b, int n, uint32_t ok_ms) {
  const uint32_t m = (g_ui.now - ok_ms) / 60000u;
  if (m < 10) snprintf(b, n, "%luM", (unsigned long)m);
  else if (m < 120) snprintf(b, n, "%lu MIN AGO", (unsigned long)m);
  else snprintf(b, n, "%luH AGO", (unsigned long)(m / 60));
}

// ---------------------------------------------------------------- widgets --
static uint16_t s_batt_fill = C_TXT;
void w_battery(int x, int y, int pct, uint16_t c) {
  gfx_rrect_frame(x, y, 18, 10, 2, c);
  gfx_rect(x + 18, y + 3, 2, 4, c);
  if (pct >= 0) {
    const int fw = (14 * (pct > 100 ? 100 : pct)) / 100;
    if (fw > 0) gfx_rrect(x + 2, y + 2, fw, 6, 1, pct > 20 ? s_batt_fill : C_DN);
  }
}
void w_wifi(int cx, int cy, int lvl, uint16_t on, uint16_t off) {
  for (int i = 0; i < 3; i++) gfx_arc((float)cx, (float)cy, 5.0f + i * 3 + 1, 2, 225, 315, i < lvl ? on : off);
  gfx_circle((float)cx, (float)cy - 0.5f, 1.5f, lvl ? on : C_DN);
  if (lvl == 0) gfx_line((float)cx - 6, (float)cy - 11, (float)cx + 6, (float)cy, 2, C_DN);
}
static uint8_t app_job(uint8_t app) {
  switch (app) {
    case APP_WEATHER: case APP_FORECAST: case APP_SKY: return JOB_WX;
    case APP_AIR: return JOB_AIR;
    case APP_CRYPTO: return JOB_CRYPTO;
    case APP_NEWS: return JOB_NEWS;
    case APP_STOCKS: return JOB_STOCKS;
    case APP_CRICKET: return JOB_CRICKET;
    default: return 0xFF;
  }
}
void w_status(const char *name, const char *badge, bool home) {
  if (!gfx_vis(0, 24)) return;
  const uint16_t bg = C_BG, txt = home ? H_TXT : C_TXT, mut = home ? H_MUT : C_MUT;
  gfx_rect(0, 0, GFX_W, 24, bg);
  // App screens keep the 16 px icon at x=9 (Oracle's header must stay that way).
  // Menu and Settings draw a left chevron in the status band; the tap target is
  // the 44 px corner, which is taller than the band.
  const bool back_chev = !home && g_ui.scr != SCR_APP && g_ui.scr != SCR_BOOT && g_ui.scr != SCR_HOME;
  if (!home && g_ui.scr == SCR_APP) {
    w_icon(k_apps[g_ui.app].icon, 16, 9, 4, C_ACC);
    gfx_micro(31, 7, name, txt);
  } else if (back_chev) {
    gfx_line(18, 5, 7, 12, 2.4f, C_ACC);
    gfx_line(7, 12, 18, 19, 2.4f, C_ACC);
    gfx_micro(26, 7, name, txt);
  } else {
    gfx_rrect(12, 7, 3, 10, 1, home ? A_HOME : C_ACC);
    gfx_micro(21, 7, name, txt);
  }
  const int xr = GFX_W - 12;
  const AuraData &d = g_ui.d;
  s_batt_fill = home ? H_UP : C_TXT;
  w_battery(xr - 20, 7, d.batt.pct, mut);
  char pct[8];
  if (d.batt.pct >= 0) snprintf(pct, sizeof(pct), "%d%%", d.batt.pct);
  else snprintf(pct, sizeof(pct), "--%%");
  const int pw = gfx_micro(xr - 25, 7, pct, mut, GA_R);
  const int xw = xr - 25 - pw - 12;
  const int lvl = status_wifi_lvl();
  w_wifi(xw, 17, lvl, mut, home ? H_SURF2 : C_TILE_HI);
  int xb = xw - 14;
  if (d.ble.connected) {  // small BT dot when a PC is linked
    gfx_circle((float)xb + 2, 12, 2.5f, C_ACC);
    xb -= 10;
  }
  // badge
  char auto_b[20];
  const char *b = badge;
  uint16_t bc = C_MUT;
  bool stale = false;
  if (!b && g_ui.scr == SCR_APP) {
    const uint8_t j = app_job(g_ui.app);
    b = "";
    if (j != 0xFF) {
      const JobInfo &ji = d.job[j];
      if (ji.st == JS_LOADING) b = "SYNC";
      else if (ji.ok_ms) {
        fmt_age(auto_b, sizeof(auto_b), ji.ok_ms);
        b = auto_b;
        stale = job_age_min(j) >= 10;
      } else if (ji.st == JS_RETRY || ji.st == JS_FAIL || ji.st == JS_HEAP) b = "RETRY";
    }
  }
  if (b && b[0]) {
    if (stale) bc = C_WARN;
    const int bw = gfx_micro_w(b, true) + 12, bx = xb - bw;
    gfx_rrect(bx, 5, bw, 14, 6, home ? H_SURF : C_TILE);
    gfx_rrect_frame(bx, 5, bw, 14, 6, blend565(C_TILE, bc, stale ? 130 : 90));
    gfx_micro(bx + 6, 8, b, bc, GA_L, true);
  }
  gfx_hline(0, 23, GFX_W, C_LINE);  // v3.1.2: plain hairline on Home too (was the aurora gradient)
}
void w_card(int x, int y, int w, int h, int r, bool hi, bool outline) {
  PROF_SCOPE(PL_TILES);
  if (!gfx_vis(y, h)) return;
  const uint16_t f = hi ? C_TILE_HI : C_TILE;
  const uint16_t top = hi ? C_TILE_HI_TOP : C_TILE_T;
  gfx_tile(x, y, w, h, r, top, f, hi ? blend565(top, C_TXT, 24) : C_TILE_RIM);
  if (outline) gfx_rrect_frame(x, y, w, h, r, C_FOCUS_BORDER);
}
int w_chip(int x, int y, const char *s, bool on, uint16_t c) {
  const int w = gfx_micro_w(s, true) + 12;
  if (!gfx_vis(y, 14)) return w;
  gfx_rrect(x, y, w, 14, 7, blend565(C_BG, c, on ? 70 : 25));
  gfx_rrect_frame(x, y, w, 14, 7, blend565(C_BG, c, on ? 160 : 70));
  gfx_micro(x + 6, y + 3, s, on ? C_TXT : C_MUT, GA_L, true);
  return w;
}
void w_footer(const char *s) {
  if (!gfx_vis(224, 14)) return;
  gfx_micro(GFX_W / 2, 226, s, blend565(C_BG, C_DIM, 170), GA_C, true);
}
void w_page_dots(int idx, int n, int y, uint16_t on_col, uint16_t off_col) {
  if (!gfx_vis(y - 3, 6)) return;
  const int w = n * 10 + 8;
  int x = GFX_W / 2 - w / 2;
  for (int i = 0; i < n; i++) {
    if (i == idx) { gfx_rrect(x, y - 2, 14, 4, 2, on_col); x += 18; }
    else { gfx_circle((float)x + 2, (float)y, 2, off_col); x += 10; }
  }
}
void w_icon(uint8_t icon, int size, int x, int y, uint16_t c, uint32_t step, float scale) {
  PROF_SCOPE(PL_FACE);
  if (!gfx_vis(y - 6, size + 12)) return;
  const int cx = x + size / 2, cy = y + size / 2;
  const IconDef *d = icon_def(icon);
  if (!d) return;
  const uint8_t f = d->seq_len ? d->seq[step % d->seq_len] : 0;
  const uint8_t *m;
  if (d->procedural) m = step ? (size >= 48 ? d->base48 : d->base24) : (size >= 48 ? d->s48 : (size >= 24 ? d->s24 : d->s16));
  else if (size >= 48) m = (step && d->f48) ? d->f48[f & 7] : d->s48;
  else if (size >= 24) m = (step && d->f24) ? d->f24[f & 7] : d->s24;
  else m = d->s16;
  gfx_a4_scaled((float)cx, (float)cy, size, size, m, c, scale);
  if (d->procedural && step) {  // moving hand (icons_anim_a4.h header rules)
    const float N = (float)size, st = size >= 48 ? 3.0f : 2.0f;
    if (icon == IC_HOME) {
      const float hx = x + 12 * N / 24, hy = y + 15.4f * N / 24, L = 2.5f * N / 24, a = (-90 + 45.0f * f) * 0.0174533f;
      gfx_line(hx, hy, hx + cosf(a) * L, hy + sinf(a) * L, 0.55f * st, C_TILE_HI);
    } else {
      const float hx = x + 12 * N / 24, hy = y + 13.6f * N / 24, L = 4.4f * N / 24, a = (-90 + 360.0f * f / 8) * 0.0174533f;
      gfx_line(hx, hy, hx + cosf(a) * L, hy + sinf(a) * L, st, c);
    }
  }
}
void w_spinner(int cx, int cy, int r, uint16_t c, uint16_t bg) {
  const float a = fmodf(g_ui.now * 0.42f, 360.0f);
  for (int k = 0; k < 3; k++) gfx_arc((float)cx, (float)cy, (float)r, 5, a - k * 75, a - k * 75 + 60, blend565(bg, c, (uint8_t)(240 - k * 75)));
}
void w_toast_draw(void) {
  if ((int32_t)(s_toast_until - g_ui.now) <= 0 || !s_toast[0]) return;
  if (!gfx_vis(190, 34)) return;
  const int w = gfx_text_w(&AF_LABEL, s_toast) + 32, x = (GFX_W - w) / 2;
  gfx_rrect(x, 192, w, 28, 10, C_TILE_HI);
  gfx_rrect_frame(x, 192, w, 28, 10, C_WARN);
  gfx_rrect(x + 8, 199, 3, 14, 1, C_WARN);
  gfx_text_m(&AF_LABEL, x + 18, 206, s_toast, C_TXT);
}

// ---------------------------------------------------------- weather glyphs --
uint8_t wx_kind(uint8_t c) {
  if (c <= 1) return 0;
  if (c <= 3) return 1;
  if (c == 45 || c == 48) return 5;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return 4;
  if (c >= 95) return 3;
  return 2;
}
const char *wx_text(uint8_t c) {
  switch (c) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm, hail";
    default: return "--";
  }
}
void w_moon(float cx, float cy, float r, float phase, uint16_t bg) {
  if (!gfx_vis((int)(cy - r - 1), (int)(2 * r + 3))) return;
  // lit part: disc minus an offset disc (phase 0 new, 0.5 full, 1 new)
  const uint16_t lit = RGB565C(226, 230, 238), dark = RGB565C(24, 30, 42);
  gfx_circle(cx, cy, r, dark);
  const float k = cosf(phase * 6.2831853f);  // 1 new .. -1 full
  const bool wax = phase < 0.5f;
  for (int y = (int)-r; y <= (int)r; y++) {
    const float w = sqrtf(r * r - (float)y * y);
    const float term = w * k;
    float xa, xb;
    if (wax) { xa = term; xb = w; } else { xa = -w; xb = -term; }
    for (int x = (int)floorf(xa); x <= (int)ceilf(xb); x++) {
      if (x < xa - 0.5f || x > xb + 0.5f) continue;
      const float mx = x / r, my = y / r;
      const float crater = (mx + 0.25f) * (mx + 0.25f) + (my + 0.3f) * (my + 0.3f) < 0.05f ? 1.f : 0.f;
      gfx_px((int)(cx + x), (int)(cy + y), crater ? RGB565C(160, 166, 178) : lit);
    }
  }
  gfx_ring(cx, cy, r + 0.5f, 1, RGB565C(70, 80, 96));
  (void)bg;
}
void w_wx_glyph(int cx, int cy, uint8_t wmo, bool day, int size, uint16_t wind_x10) {
  PROF_SCOPE(PL_FACE);
  wx_draw(cx, cy, wmo, day, wind_x10, size);  // A4 layered glyph (glyphs_wx_a4.h)
}

// ----------------------------------------------------------------- states --
static void sk(int x, int y, int w, int h, uint8_t k) {
  gfx_rrect(x, y, w, h, 6, blend565(C_TILE, C_TILE_HI, k));
}
void st_loading(void) {
  // skeleton (state_loading_skeleton) with a moving shimmer
  w_card(10, 30, 300, 78);
  const uint8_t ph = (uint8_t)(((g_ui.now / 4) % 256));
  const uint8_t k = (uint8_t)(140 + 100 * (ph < 128 ? ph : 255 - ph) / 128);
  sk(22, 40, 98, 8, k); sk(22, 60, 148, 20, k); sk(190, 58, 108, 38, (uint8_t)(k / 2)); sk(22, 90, 178, 6, k);
  for (int i = 0; i < 3; i++) {
    const int y = 116 + i * 36;
    w_card(10, y, 300, 30, 8);
    sk(18, y + 7, 16, 16, k); sk(42, y + 8, 58, 6, k); sk(130, y + 8, 70, 14, (uint8_t)(k / 2)); sk(230, y + 8, 70, 12, k);
  }
  w_footer("LOADING" AF_ELL);
}
void st_missing_key(const char *what) {
  w_card(30, 48, 260, 142);
  const float kx = 160, ky = 92;
  gfx_ring(kx - 16, ky, 12, 4, C_WARN);
  gfx_line(kx - 4, ky, kx + 26, ky, 4, C_WARN);
  gfx_line(kx + 16, ky, kx + 16, ky + 9, 4, C_WARN);
  gfx_line(kx + 24, ky, kx + 24, ky + 7, 4, C_WARN);
  char b[48];
  snprintf(b, sizeof(b), "Add %s key in setup", what);
  gfx_text_m(&AF_ROW, 160, 134, b, C_TXT, GA_C);
  gfx_text_m(&AF_BODY_S, 160, 154, "Keys are stored on the device only", C_MUT, GA_C);
  const int w = gfx_micro_w("PRESS FOR SETUP", true) + 12;
  w_chip(160 - w / 2, 166, "PRESS FOR SETUP", true, C_UP);
}
void st_empty(const char *title, const char *l1, const char *l2, uint8_t icon) {
  w_card(30, 48, 260, 142);
  w_icon(icon, 24, 148, 74, blend565(C_BG, C_ACC, 200));
  gfx_text_m(&AF_ROW_FOCUS, 160, 124, title, C_TXT, GA_C);
  if (l1) gfx_micro(160, 142, l1, C_MUT, GA_C);
  if (l2) gfx_micro(160, 158, l2, C_DIM, GA_C);
}
void st_retrying(const char *title, const JobInfo *j) {
  w_card(30, 54, 260, 132);
  w_spinner(160, 94, 18, C_DN, C_TILE);
  gfx_text_m(&AF_ROW_FOCUS, 160, 134, title, C_TXT, GA_C);
  char b[56];
  const int32_t s = (int32_t)(j->next_ms - g_ui.now) / 1000;
  if (j->st == JS_HEAP) snprintf(b, sizeof(b), "RAM tight " AF_DOT " press retries now");
  else if (j->st == JS_PAUSED) snprintf(b, sizeof(b), "Paused while setup runs");
  else if (j->st == JS_RETRY) snprintf(b, sizeof(b), "Retrying in %ld s " AF_DOT " attempt %u of 3", (long)(s > 0 ? s : 0), (unsigned)j->attempt);
  else snprintf(b, sizeof(b), "Next try in %ld min", (long)((s > 0 ? s : 0) / 60));
  gfx_text_m(&AF_BODY_S, 160, 156, b, C_MUT, GA_C);
  char e[40];
  // v3.1.6 p0p1: say why for a refused key / spent quota (net sets http 401/403/429, also synthetic for AV)
  if (j->http == 401 || j->http == 403) snprintf(e, sizeof(e), "KEY REJECTED " AF_DOT " CHECK WI-FI SETUP");
  else if (j->http == 429) snprintf(e, sizeof(e), "API LIMIT REACHED " AF_DOT " HTTP 429");
  else snprintf(e, sizeof(e), "ERR %d " AF_DOT " HTTP %u", (int)j->err, (unsigned)j->http);
  gfx_micro(160, 168, e, C_DIM, GA_C, true);
  w_footer("PRESS: RETRY NOW   HOLD: BACK");
}
void st_needs_pc(const char *what) {
  w_card(30, 48, 260, 142);
  // monitor glyph
  gfx_rrect_frame(140, 70, 40, 28, 4, C_ACC);
  gfx_rect(156, 98, 8, 6, C_ACC);
  gfx_rect(148, 104, 24, 2, C_ACC);
  gfx_text_m(&AF_ROW, 160, 128, "Needs the AURA PC app", C_TXT, GA_C);
  gfx_text_m(&AF_BODY_S, 160, 148, what, C_MUT, GA_C);
  gfx_micro(160, 164, "PC LINK ARRIVES IN P4", C_DIM, GA_C, true);
}
void st_offline(void) {
  buddy_draw_expr(BX_SAD, 160, 96, 0.72f, C_BG);  // v3.1.4: Buddy v2 still (sad)
  const bool nocreds = g_ui.d.net.wifi_state == WIFI_ST_NO_CREDS;
  gfx_text_m(&AF_ROW_FOCUS, 160, 182, nocreds ? "No Wi-Fi set up" : "Offline", C_TXT, GA_C);
  gfx_micro(160, 196, nocreds ? "SETTINGS " AF_CHEV " WI-FI SETUP" : "RECONNECTING" AF_ELL, C_ACC, GA_C);
}
bool w_job_gate(uint8_t job, bool have) {
  if (have) return true;
  const AuraData &d = g_ui.d;
  if (d.net.wifi_state != WIFI_ST_UP) {
    if (d.net.wifi_state == WIFI_ST_CONNECTING) st_loading();
    else st_offline();
    return false;
  }
  const JobInfo &j = d.job[job];
  if (j.st == JS_NEVER || j.st == JS_LOADING || j.st == JS_OK) st_loading();
  else st_retrying("Couldn't load", &j);
  return false;
}
void w_stale_pill(uint8_t job, int x, int y) {
  const uint32_t m = job_age_min(job);
  if (m == 0xFFFFFFFFu || m < 10) return;
  char b[32];
  if (m < 120) snprintf(b, sizeof(b), "UPDATED %lu MIN AGO", (unsigned long)m);
  else snprintf(b, sizeof(b), "UPDATED %luH AGO", (unsigned long)(m / 60));
  const int w = gfx_micro_w(b, true) + 16;
  if (x < 0) x = GFX_W - 12 - w;
  gfx_rrect(x, y, w, 20, 10, blend565(C_BG, C_WARN, 60));
  gfx_rrect_frame(x, y, w, 20, 10, C_WARN);
  gfx_micro(x + 8, y + 6, b, C_WARN, GA_L, true);
}

int w_text_ell(const GFXfont *f, int x, int ym, const char *s, int maxw, uint16_t c, GfxAlign al) {
  if (gfx_text_w(f, s) <= maxw) return gfx_text_m(f, x, ym, s, c, al);
  char b[128];
  int n = (int)strlen(s);
  if (n > 120) n = 120;
  const int ew = gfx_text_w(f, AF_ELL);
  while (n > 0) {
    memcpy(b, s, n);
    b[n] = 0;
    if (gfx_text_w(f, b) + ew <= maxw) break;
    n--;
  }
  while (n > 0 && b[n - 1] == ' ') n--;
  b[n] = (char)0x81;
  b[n + 1] = 0;
  return gfx_text_m(f, x, ym, b, c, al);
}

// ------------------------------------------------------------ app screen --
static void app_enter(uint32_t now) {
  const AppDef *a = &k_apps[g_ui.app];
  if (a->enter) a->enter(now);
}
static void app_leave(void) {
  const AppDef *a = &k_apps[g_ui.app];
  if (a->leave) a->leave();
}
static void app_input(const UiIn &in, uint32_t now) {
  const AppDef *a = &k_apps[g_ui.app];
  if (a->input) a->input(in, now);
}
static void app_draw(void) {
  const AppDef *a = &k_apps[g_ui.app];
  if (a->draw) a->draw();
}
// v3.1.4: per-frame app tick (Buddy rig, Oracle think hook) and band keys for the Buddy app
// only (dirty-window drawing); every other app keeps drawing all bands (key 0).
static void app_tick(uint32_t now) {
  if (g_ui.app == APP_BUDDY) buddyapp_tick(now);
  else if (g_ui.app == APP_ORACLE) oracle_tick(now);
  else if (g_ui.app == APP_PC) pc_tick(now);
}
static uint32_t app_band_key(int b, BandWin *w) { return g_ui.app == APP_BUDDY ? buddyapp_band_key(b, w) : 0; }
// Holds Buddy off while a countdown is in progress, Oracle is mid-reply, or a PC is linked on Macro.
// Pause (focus st 2) and a finished reply do not hold: normal 45 s takeover still applies.
static bool app_idle_lock(void) {
  if (g_ui.app == APP_FOCUS && (g_focus.st == 1 || g_focus.st == 3)) return true;
  if (g_ui.app == APP_ORACLE && oracle_idle_locked()) return true;
  if (g_ui.app == APP_MACRO && g_ui.d.ble.connected) return true;
  if (pc_token_visible()) return true;
  return false;
}
const Screen SCR_APP_DEF = {"APP", app_enter, app_leave, app_input, app_tick, app_draw, true, false, app_band_key, app_idle_lock};

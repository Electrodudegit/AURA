// host_captures.cpp - the list of screens/states written to captures/.
#include "apps.h"
#include <string.h>
#include <stdio.h>
AuraData &host_data(void);
struct tm &host_tm(void);
void host_set_tv(bool tv);
void host_shot(const char *name, uint32_t now);
void host_frame(uint32_t now);
void host_reset_data(void);
void boot_debug_set(uint8_t ph, uint32_t el, bool online, uint8_t sync_i);
void buddy_debug_set(int bx, float morph_u, uint32_t now);
void menu_debug_focus(uint8_t f, bool last);

static uint32_t T = 200000;
static void boot_caps(void) {
  ui_go(SCR_BOOT);
  host_frame(T);
  boot_debug_set(0, 900, false, 0); host_shot("boot_1_matrix", T);
  boot_debug_set(0, 1900, false, 0); host_shot("boot_2_matrix_late", T);
  boot_debug_set(2, 250, false, 0); host_shot("boot_3_aurora_wake", T);
  boot_debug_set(3, 260, false, 0); host_shot("boot_4_wordmark_rise", T);
  boot_debug_set(4, 200, false, 0); host_shot("boot_5_wordmark_shimmer", T);
  boot_debug_set(5, 170, false, 0); host_shot("boot_6_byline", T);
  boot_debug_set(6, 1200, false, 0); host_shot("boot_7_connecting", T);
  boot_debug_set(7, 120, true, 0); host_shot("boot_8_online_pulse", T);
  boot_debug_set(8, 300, true, 0); host_shot("boot_9_online_timewait", T);
  boot_debug_set(9, 50, true, 3); host_shot("boot_10_syncing", T);
  boot_debug_set(11, 300, false, 0); host_shot("boot_11_offline", T);
}
static void home_caps(void) {
  static const char *nm[4] = {"home_F1_splitflap", "home_F2_buddyclock", "home_F5_weather", "home_F6_analog"};
  for (int f = 0; f < 4; f++) {
    g_ui.prefs.face = (uint8_t)f;
    ui_go(SCR_HOME);
    host_frame(T); host_frame(T + 2000);
    host_shot(nm[f], T + 2600);
    T += 10000;
  }
  // F1 flip mid-animation
  g_ui.prefs.face = 0;
  ui_go(SCR_HOME);
  host_frame(T);
  host_tm().tm_min = 33;
  host_frame(T + 40);
  host_shot("home_F1_flip_mid", T + 180);
  host_shot("home_F1_flip_late", T + 400);
  host_tm().tm_min = 32;
  T += 10000;
  // no time / no data
  host_set_tv(false);
  AuraData save = host_data();
  host_data().wx.valid = false; host_data().air.valid = false;
  ui_go(SCR_HOME); host_frame(T); host_shot("home_F1_nodata", T + 1500);
  host_data() = save; host_set_tv(true);
  T += 10000;
}
static void menu_caps(void) {
  g_ui.prefs.menu_list = 0;
  g_ui.prefs.last_app = APP_CRYPTO;
  ui_go(SCR_MENU); host_frame(T);
  menu_debug_focus(APP_CRYPTO, true); host_shot("menu_grid_p1_last", T + 500);
  menu_debug_focus(APP_FOCUS, false); host_shot("menu_grid_p2_focus", T + 900);
  menu_debug_focus(APP_SETTINGS, false); host_shot("menu_grid_p3", T + 1300);
  T += 10000;
  g_ui.prefs.menu_list = 1;
  g_ui.prefs.last_app = APP_HOME;
  ui_go(SCR_MENU); host_frame(T);
  menu_debug_focus(APP_HOME, false); host_shot("menu_list_home", T + 500);
  g_ui.prefs.last_app = APP_CRYPTO;  // Q02: the LAST chip needs last_app == the focused row
  menu_debug_focus(APP_CRYPTO, true); host_shot("menu_list_crypto_last", T + 900);
  menu_debug_focus(APP_SETTINGS, false); host_shot("menu_list_end", T + 1300);
  g_ui.prefs.menu_list = 0;
  T += 10000;
}
static void buddy_caps(void) {
  // v3.1.4 Buddy v2: IDLE + the 10 expressions (settled), the morph midpoint, the app hint
  const uint32_t T0 = T;  // later captures keep their v3.1.3 timestamps (T advances by 30 s here as before)
  ui_open_app(APP_BUDDY); host_frame(T);
  host_shot("buddy_app_live", T + 1000);
  for (int e = 0; e < BX_COUNT; e++) {
    char b[48];
    snprintf(b, sizeof(b), "buddy_%02d_%s", e, buddy_expr_name((uint8_t)e));
    for (char *p = b; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    const uint32_t t = T + 5000 + (uint32_t)e * 3200;  // same breathing phase in every shot
    buddy_debug_set(e, -1.0f, t);
    host_shot(b, t);
  }
  {
    const uint32_t t = T + 5000 + 11 * 3200;
    buddy_debug_set(BX_HAPPY, 0.5f, t); host_shot("buddy_morph_mid_idle_to_happy", t);
    buddy_debug_set(BX_LOVE, 0.5f, t); host_shot("buddy_morph_mid_idle_to_love", t);
  }
  {  // F2 press / tap: pop (scale 1.10 peak at +40 ms), shot one frame later
    const uint32_t t = T + 50000;
    const uint8_t f0 = g_ui.prefs.face;
    g_ui.prefs.face = 1; ui_go(SCR_HOME); host_frame(t); host_frame(t + 2000);
    buddy_input(&g_rig, BI_PRESS, 0, t + 2000); host_shot("home_F2_pop_happy", t + 2040);
    buddy_input(&g_rig, BI_TAP, 0, t + 3000); host_shot("home_F2_pop_surprised", t + 3040);
    g_ui.prefs.face = f0;
  }
  {  // v3.1.5: app pops at the clamped peak (+40 ms): press = happy, tap forced to surprised (tallest eyes)
    const uint32_t t = T + 60000;
    ui_open_app(APP_BUDDY); host_frame(t); host_frame(t + 2000);
    buddy_input(&g_rig, BI_PRESS, 0, t + 2000); host_shot("buddy_app_pop_happy_max", t + 2040);
    buddy_input(&g_rig, BI_TAP, 0, t + 5000); g_rig.react_expr = BX_SURPRISED;
    host_shot("buddy_app_pop_surprised_max", t + 5040);
    printf("[pop] app scale=%.4f (frame %.4f)\n", g_rig.pop_max, g_rig.fr.scale);
    g_ui.prefs.face = 1; ui_go(SCR_HOME); host_frame(t + 9000);
    buddy_input(&g_rig, BI_TAP, 0, t + 9000); host_shot("home_F2_pop_surprised_max", t + 9040);
    printf("[pop] F2 scale=%.4f (frame %.4f)\n", g_rig.pop_max, g_rig.fr.scale);
    g_ui.prefs.face = 0;
    float ta, tf;
    const float ka = buddy_pop_max(BRM_APP, 104, 1.0f, &ta), kf = buddy_pop_max(BRM_F2, 74.6f, 0.74f, &tf);
    printf("[pop] clamp app=%.4f worst_face_top=%.2f | F2=%.4f worst_face_top=%.2f\n", ka, ta, kf, tf);
  }
  T = T0 + 20000;
  ui_go(SCR_BUDDY); host_frame(T); buddy_debug_set(-1, -1.0f, T); host_shot("buddy_idle_takeover", T + 3000);
  ui_go(SCR_SAVER); host_frame(T + 4000); host_shot("saver_splitflap", T + 5000);
  T += 10000;
}
void crypto_debug_hist(void);
void settings_debug_page(uint8_t page, uint8_t row);
void tgrid_debug_set(uint16_t n, uint16_t h);
void oracle_debug_view(uint32_t seq, float tw);
void oracle_debug_focus(uint8_t f);
void oracle_debug_draft(const char *t);
void oracle_debug_keypad(uint8_t mode, uint8_t focus);
static void fresh(uint32_t now) {
  for (int j = 0; j < JOB_COUNT; j++) { host_data().job[j].st = JS_OK; host_data().job[j].ok_ms = now - 3 * 60000; host_data().job[j].next_ms = now + 600000; }
}
static void inp(uint8_t type, int d = 0, int x = 0, int y = 0) {
  UiIn in; memset(&in, 0, sizeof(in));
  in.type = type; in.d = (int16_t)d; in.x = (int16_t)x; in.y = (int16_t)y; in.x0 = (int16_t)x; in.y0 = (int16_t)y;
  ui_input(in);
}
static void app_shot(uint8_t app, const char *name, uint32_t dt = 1500) {
  ui_open_app(app); host_frame(T); host_shot(name, T + dt); T += 10000;
}
// Generic data states for one data app.
static void data_states(uint8_t app, uint8_t job, const char *base) {
  char b[64];
  AuraData save = host_data();
  fresh(T); snprintf(b, sizeof(b), "%s_normal", base); app_shot(app, b);
  // stale (cached 47 min)
  for (int j = 0; j < JOB_COUNT; j++) host_data().job[j].ok_ms = T - 47 * 60000;
  snprintf(b, sizeof(b), "%s_stale", base); app_shot(app, b);
  // loading (no data yet)
  host_data().wx.valid = host_data().air.valid = host_data().crypto.valid = host_data().news.valid = false;
  host_data().stocks.valid = host_data().cricket.valid = false;
  host_data().job[job].st = JS_LOADING;
  snprintf(b, sizeof(b), "%s_loading", base); app_shot(app, b);
  host_data().job[job].st = JS_RETRY; host_data().job[job].attempt = 2; host_data().job[job].err = -3; host_data().job[job].next_ms = T + 20000;
  snprintf(b, sizeof(b), "%s_retrying", base); app_shot(app, b);
  host_data().net.wifi_state = WIFI_ST_BACKOFF;
  snprintf(b, sizeof(b), "%s_offline", base); app_shot(app, b);
  host_data() = save; fresh(T);
}
static void app_caps(void) {
  fresh(T);
  data_states(APP_WEATHER, JOB_WX, "app_weather");
  data_states(APP_FORECAST, JOB_WX, "app_forecast");
  data_states(APP_SKY, JOB_WX, "app_sky");
  data_states(APP_AIR, JOB_AIR, "app_air");
  crypto_debug_hist();
  data_states(APP_CRYPTO, JOB_CRYPTO, "app_crypto");
  data_states(APP_NEWS, JOB_NEWS, "app_news");
  // keyed apps
  host_data().keys = KeysData{false, false, false, false, false, 0};
  app_shot(APP_STOCKS, "app_stocks_missing_key");
  app_shot(APP_CRICKET, "app_cricket_missing_key");
  host_data().oracle.key_ok = false;
  app_shot(APP_ORACLE, "app_oracle_missing_key");
  host_data().keys = KeysData{true, true, true, true, true, 0};
  host_data().oracle.key_ok = true;
  data_states(APP_STOCKS, JOB_STOCKS, "app_stocks");
  data_states(APP_CRICKET, JOB_CRICKET, "app_cricket");
  app_shot(APP_ORACLE, "app_oracle_prompts");
  ui_open_app(APP_ORACLE); host_frame(T);
  oracle_debug_focus(4); host_shot("app_oracle_type_row", T + 400);
  oracle_debug_draft("Will it rain at 5?"); host_shot("app_oracle_type_draft", T + 800);
  oracle_debug_draft(""); oracle_debug_keypad(0, 0); host_shot("app_oracle_keypad", T + 1200);
  oracle_debug_draft("Pack an umbrella?"); oracle_debug_keypad(0, 16); host_shot("app_oracle_keypad_draft", T + 1600);
  oracle_debug_keypad(1, 19); host_shot("app_oracle_keypad_shift", T + 2000);
  oracle_debug_keypad(3, 0); host_shot("app_oracle_keypad_num", T + 2400);
  oracle_debug_keypad(0, 30); host_shot("app_oracle_keypad_ask", T + 2800);
  T += 10000;
  {  // Oracle (v3.1.1): synthetic states, no network
    OracleData &o = host_data().oracle;
    memset(&o, 0, sizeof(o));
    snprintf(o.model, sizeof(o.model), "grok-3-mini");
    o.key_ok = true;
    o.provider = 1;
    ui_open_app(APP_ORACLE); host_frame(T); inp(IN_CLICK); host_shot("app_oracle_needs_pc", T + 800); T += 10000;
    o.provider = 0;
    app_shot(APP_ORACLE, "app_oracle_prompts_device");
    struct { const char *name; uint8_t st; uint16_t http; const char *reason; const char *text; float tw; uint32_t busy; } k[] = {
      {"app_oracle_thinking", OR_THINKING, 0, "", "", 0, 0},
      {"app_oracle_streaming", OR_STREAM, 200, "", "Light linen and breathable cotton are your friends today: 31\x7f" "C and humid. Carry an umbrella", 60, 0},
      {"app_oracle_done", OR_DONE, 200, "", "Go with a light cotton shirt, chinos and breathable sneakers: it is 31\x7f" "C and humid, so skip the jacket. "
        "The rain chance is 60% after 5 pm, so pack a compact umbrella and keep sunglasses handy for the bright afternoon. "
        "Stay hydrated and keep a spare T-shirt if you will be outside for long.", 999, 0},
      {"app_oracle_busy", OR_BUSY, 429, "RATE LIMITED", "", 0, 17000},
      {"app_oracle_key_rejected", OR_KEYBAD, 401, "KEY REJECTED (401)", "", 0, 0},
      {"app_oracle_error", OR_ERROR, 0, "NO REPLY IN 20 S", "", 0, 0},
    };
    for (auto &c : k) {
      ui_open_app(APP_ORACLE); host_frame(T);
      o.seq = 7; o.st = c.st; o.http = c.http;
      snprintf(o.reason, sizeof(o.reason), "%s", c.reason);
      snprintf(o.text, sizeof(o.text), "%s", c.text);
      o.n = (uint16_t)strlen(o.text);
      o.retry_until_ms = T + 600 + c.busy;
      oracle_debug_view(7, c.tw);
      host_frame(T + 300);  // v3.1.4: let the rig morph into THINK (240 ms) before the shot
      host_shot(c.name, T + 600); T += 10000;
    }
    memset(&o, 0, sizeof(o));
  }
  host_data().keys = KeysData{false, false, false, false, false, 0};
  // focus
  g_focus = FocusState{0, 0, 0, 0}; app_shot(APP_FOCUS, "app_focus_idle");
  ui_open_app(APP_FOCUS); host_frame(T);
  g_focus.st = 1; g_focus.t_end = T + 17 * 60000 + 42000; g_focus.sessions = 2;
  host_shot("app_focus_running", T + 1000); T += 10000;
  g_focus = FocusState{0, 0, 0, 0};
  app_shot(APP_WORLD, "app_world");
  app_shot(APP_SYSTEM, "app_system", 4000);
  // macro (v3.1.6 P1 sheet): off / idle / pairing / connected / focus+flash / dropped / now playing
  g_ui.prefs.pc_keys = 0; host_data().ble = BleData{false, 0, false, false, 0, false, 0, 0};
  app_shot(APP_MACRO, "app_macro_pckeys_off");
  g_ui.prefs.pc_keys = 1; host_data().ble = BleData{true, 0, false, false, 0, false, 0, 0};
  app_shot(APP_MACRO, "app_macro_idle_timeout");
  host_data().ble = BleData{true, 97000, true, true, 0, false, 0, 0};
  app_shot(APP_MACRO, "app_macro_pairing");
  host_data().ble = BleData{true, 0, true, false, 1, true, 42, 0};
  app_shot(APP_MACRO, "app_macro_connected");
  ui_open_app(APP_MACRO); host_frame(T); macro_debug_flash(2, 2); host_shot("app_macro_v316_flash_next", T + 50); T += 10000;
  host_data().ble = BleData{true, 0, true, false, 1, true, 128, 3};
  ui_open_app(APP_MACRO); host_frame(T); macro_debug_flash(255, 4); host_shot("app_macro_v316_focus_mute_dropped", T + 50); T += 10000;
  host_data().ble = BleData{true, 0, true, false, 1, true, 42, 0};
  // now playing: companion session, no track -> "Not playing"; then a track with 48x48 art
  g_ui.prefs.comp_on = 1;
  host_data().comp.on = host_data().comp.listening = host_data().comp.paired = host_data().comp.session = true;
  app_shot(APP_MACRO, "app_macro_np_idle");
  {
    static uint8_t art[48 * 48 * 2];
    for (int y = 0; y < 48; y++)
      for (int x = 0; x < 48; x++) {
        const uint16_t c = (uint16_t)(((x * 31 / 47) << 11) | ((y * 40 / 47 + 10) << 5) | (31 - x * 20 / 47));
        art[(y * 48 + x) * 2] = (uint8_t)c; art[(y * 48 + x) * 2 + 1] = (uint8_t)(c >> 8);
      }
    ds_comp_art_begin(); ds_comp_art_write(0, art, sizeof(art)); ds_comp_art_commit(48, 48);
    CompNowData &nw = host_data().comp_now;
    nw.playing = nw.live = true; nw.art_w = nw.art_h = 48; nw.art_seq = 1; nw.ms = T;
    snprintf(nw.title, sizeof(nw.title), "Midnight City");
    snprintf(nw.artist, sizeof(nw.artist), "M83");
    app_shot(APP_MACRO, "app_macro_np_track");
    snprintf(nw.title, sizeof(nw.title), "A Very Long Song Title That Will Not Fit Here");
    app_shot(APP_MACRO, "app_macro_np_longtitle");
    memset(&nw, 0, sizeof(nw));
    ds_comp_art_clear();
  }
  memset(&host_data().comp, 0, sizeof(host_data().comp));
  // PC Status (v3.1.6 P1 sheet)
  g_ui.prefs.comp_on = 0;
  app_shot(APP_PC, "app_pc_off");
  g_ui.prefs.comp_on = 1;
  host_data().comp.on = true;
  host_data().comp.listening = true;
  memcpy(host_data().comp.token, "K7M2Q9XW", 9);
  snprintf(host_data().comp.devid, sizeof(host_data().comp.devid), "AURA-3F2A-7C");
  host_data().comp.ip = 0x2A01A8C0;
  app_shot(APP_PC, "app_pc_pair");
  host_data().comp.paired = true;
  host_data().comp.token[0] = 0;
  app_shot(APP_PC, "app_pc_waiting");
  host_data().comp.session = true;
  host_data().comp.live = true;
  host_data().comp.cpu = 37;
  host_data().comp.ram = 61;
  snprintf(host_data().comp.note, sizeof(host_data().comp.note), "Slack: Rahul: build is green, merging");
  for (int i = 0; i < 6; i++) {  // fill the CPU sparkline (one sample per companion frame)
    host_data().comp.cpu = (uint8_t)(30 + (i * 7) % 15);
    host_data().comp.ms = T + i * 2000;
    if (i == 0) ui_open_app(APP_PC);
    host_frame(T + i * 2000 + 10);
  }
  host_data().comp.cpu = 37;
  host_data().comp.ms = T + 12000;
  host_frame(T + 12010); host_shot("app_pc_live_comp", T + 12100); T += 20000;
  host_data().comp.ms = T;
  ui_open_app(APP_PC); host_frame(T); pc_debug_page(1, 0); host_shot("app_pc_list_comp", T + 50); T += 10000;
  host_data().comp.note[0] = 0;
  host_data().comp.ms = T;
  app_shot(APP_PC, "app_pc_allclear_comp");
  // rich sheet model (PcStatusData): RAM MB, 4 alerts, host name -> exact sheet frames
  g_ui.prefs.comp_on = 0;
  memset(&host_data().comp, 0, sizeof(host_data().comp));
  {
    PcStatusData &p = host_data().pc;
    memset(&p, 0, sizeof(p));
    p.linked = true; p.ok_ms = T; snprintf(p.host, sizeof(p.host), "OMESH-PC");
    p.cpu_pct = 37; p.ram_used_mb = 9933; p.ram_total_mb = 16384; p.notif_count = 5; p.notif_n = 4;
    const char *ap[4] = {"Slack", "Outlook", "GitHub", "WhatsApp"};
    const char *ti[4] = {"Rahul: build is green, merging v3.1.6", "Design review moved to 4:30 PM", "CI passed on aura-fw #212", "Mom: call when free"};
    const uint32_t ag[4] = {10, 720, 3600, 7200};
    for (int i = 0; i < 4; i++) { snprintf(p.notif[i].app, 12, "%s", ap[i]); snprintf(p.notif[i].title, 40, "%s", ti[i]); p.notif[i].age_s = ag[i]; }
    app_shot(APP_PC, "app_pcstatus_overview");
    p.ok_ms = T;
    ui_open_app(APP_PC); host_frame(T); pc_debug_page(1, 1); host_shot("app_pcstatus_notifications", T + 50); T += 10000;
    p.cpu_pct = 93; p.ram_used_mb = 14848; p.ok_ms = T;
    app_shot(APP_PC, "app_pcstatus_overview_hot");
    p.ok_ms = T - 120000;
    app_shot(APP_PC, "app_pcstatus_link_lost");
    p.ok_ms = T; p.notif_n = 0; p.notif_count = 0;
    ui_open_app(APP_PC); host_frame(T); pc_debug_page(1, 0); host_shot("app_pcstatus_all_clear", T + 50); T += 10000;
    memset(&p, 0, sizeof(p));
  }
}
static void settings_caps(void) {
  static const char *rn[] = {"credentials", "menu_style", "home_face", "brightness", "night_dim", "timezone", "pc_keys", "companion",
                             "phone_setup", "touch_cal", "diagnostics", "about"};
  for (int r = 0; r < 12; r++) {
    char b[48]; snprintf(b, sizeof(b), "settings_list_%s", rn[r]);
    ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(0, (uint8_t)r); host_shot(b, T + 600); T += 10000;
  }
  host_data().ble = BleData{true, 83000, true, true, 0, false, 0, 0};
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(0, 6); host_shot("settings_list_pc_keys_pairing", T + 600); T += 10000;
  g_ui.prefs.pc_keys = 0; host_data().ble = BleData{false, 0, false, false, 0, false, 0, 0};
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(0, 6); host_shot("settings_list_pc_keys_off", T + 600); T += 10000;
  g_ui.prefs.pc_keys = 1; host_data().ble = BleData{true, 0, true, false, 1, true, 42, 0};
  host_data().portal = PortalData{false, false, 0, "", ""};
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(1, 8); host_shot("settings_portal_starting", T + 600); T += 10000;
  host_data().portal = PortalData{true, false, 1, "AURA-Setup-7F3A", "k7m2-q9xw"};
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(1, 8); host_shot("settings_portal_on", T + 600); T += 10000;
  host_data().portal = PortalData{false, true, 0, "AURA-Setup-7F3A", ""};
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(1, 8); host_shot("settings_portal_saved", T + 600); T += 10000;
  host_data().portal = PortalData{false, false, 0, "", ""};
  g_ui.prefs.comp_on = 1;
  host_data().comp.on = true;
  host_data().comp.listening = true;
  host_data().comp.paired = true;
  host_data().comp.cpu = 37;
  host_data().comp.ram = 61;
  host_data().comp.ms = T;
  host_data().comp.ip = 0x2A01A8C0;
  snprintf(host_data().comp.token, sizeof(host_data().comp.token), "K7M2Q9XW");
  snprintf(host_data().comp.note, sizeof(host_data().comp.note), "Build finished");
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(4, 7); host_shot("settings_companion", T + 600); T += 10000;
  g_ui.prefs.comp_on = 0;
  memset(&host_data().comp, 0, sizeof(host_data().comp));
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(2, 10); host_shot("settings_diagnostics", T + 600); T += 10000;
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(3, 11); host_shot("settings_about", T + 600); T += 10000;
  ui_go(SCR_CAL); host_frame(T); host_shot("settings_cal_target1", T + 600);
  UiTouchRaw r; memset(&r, 0, sizeof(r)); r.down = false; ui_touch_raw(r);
  r.down = true; r.fresh = true; r.rx = 1000; r.ry = 1000;
  for (int i = 0; i < 26; i++) ui_touch_raw(r);  // complete target 1
  r.down = false; ui_touch_raw(r); r.down = true; for (int i = 0; i < 10; i++) ui_touch_raw(r);
  host_shot("settings_cal_target2_sampling", T + 900); T += 10000;
  ui_go(SCR_SETTINGS); host_frame(T);
  ui_go(SCR_TGRID); host_frame(T); tgrid_debug_set(37, 37); host_shot("settings_tgrid_running", T + 500);
  tgrid_debug_set(300, 300); host_shot("settings_tgrid_pass", T + 900); T += 10000;
  // toast
  ui_go(SCR_SETTINGS); host_frame(T); settings_debug_page(0, 6); ui_toast("Advertising for 2 min", 1500); host_shot("settings_toast", T + 300); T += 10000;
}
// v3.1.2: Groq provider captures, run last so the timeline of every earlier capture is unchanged.
static void groq_caps(void) {
  fresh(T);
  host_data().keys = KeysData{true, true, false, true, true, KEYB_GROQ};
  OracleData &o = host_data().oracle;
  // v3.1.2: Groq provider (auto-resolved: no xAI key, v2 Groq key as the built-in default)
  memset(&o, 0, sizeof(o));
  o.api = ORA_GROQ; o.api_sel = ORA_SEL_AUTO; o.key_ok = true;
  snprintf(o.model, sizeof(o.model), "llama-3.1-8b-instant");
  app_shot(APP_ORACLE, "app_oracle_groq_prompts");
  struct { const char *name; uint8_t st; uint16_t http; const char *reason; const char *text; float tw; uint32_t busy; } kg[] = {
    {"app_oracle_groq_thinking", OR_THINKING, 0, "", "", 0, 0},
    {"app_oracle_groq_done", OR_DONE, 200, "", "A light cotton shirt and chinos will do: 31\x7f" "C and humid, rain likely after 5 pm, so take a compact umbrella.", 999, 0},
    {"app_oracle_groq_busy", OR_BUSY, 429, "RATE LIMITED", "", 0, 9000},
    {"app_oracle_groq_key_rejected", OR_KEYBAD, 401, "KEY REJECTED (401)", "", 0, 0},
    {"app_oracle_groq_error", OR_ERROR, 503, "HTTP 503 SERVER ERROR", "", 0, 0},
  };
  for (auto &c : kg) {
    ui_open_app(APP_ORACLE); host_frame(T);
    o.seq = 7; o.st = c.st; o.http = c.http;
    snprintf(o.reason, sizeof(o.reason), "%s", c.reason);
    snprintf(o.text, sizeof(o.text), "%s", c.text);
    o.n = (uint16_t)strlen(o.text);
    o.retry_until_ms = T + 600 + c.busy;
    oracle_debug_view(7, c.tw);
    host_frame(T + 300);  // v3.1.4: let the rig morph into THINK (240 ms) before the shot
    host_shot(c.name, T + 600); T += 10000;
  }
  memset(&o, 0, sizeof(o));
  o.api = ORA_GROQ; o.api_sel = ORA_SEL_GROQ; o.key_ok = false;
  app_shot(APP_ORACLE, "app_oracle_groq_missing_key");
  memset(&o, 0, sizeof(o));
  host_data().keys = KeysData{false, false, false, false, false, 0};
  host_data().oracle.api = ORA_GROQ;  // v3.1.2: active Oracle provider in the Wi-Fi row subtitle
  ui_open_app(APP_SETTINGS); host_frame(T); settings_debug_page(0, 6); host_shot("settings_list_wifi_groq", T + 600); T += 10000;
  host_data().oracle.api = ORA_XAI;
}
void host_app_states(void) {
  app_caps();
  settings_caps();
  boot_caps();
  home_caps();
  menu_caps();
  buddy_caps();
  groq_caps();
}

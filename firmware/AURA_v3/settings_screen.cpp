// settings_screen.cpp - Settings list (menu style, home face, brightness, night
// dim, PC keys, Wi-Fi portal, touch calibration, diagnostics, about), plus the
// calibration and tap-grid screens (only reachable from here).
#include "apps.h"
#include "version.h"
#include "tz.h"
#include "comp_proto.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "wifi_qr.h"

enum SRow : uint8_t {
  SR_CREDS = 0, SR_MENU, SR_FACE, SR_BRIGHT, SR_NIGHT, SR_TZ, SR_PCKEYS, SR_COMP, SR_WIFI, SR_CAL, SR_DIAG, SR_ABOUT, SR_COUNT
};
enum SPage : uint8_t { SP_LIST = 0, SP_PORTAL, SP_DIAG, SP_ABOUT, SP_COMP, SP_CREDS };
static const char *const k_rn[SR_COUNT] = {"Credentials", "Menu style", "Home face", "Brightness", "Night dim", "Time zone",
                                           "PC keys (Bluetooth)", "Companion", "Phone setup", "Touch calibration",
                                           "Diagnostics", "About"};
static const char *const k_face[4] = {"SPLIT-FLAP", "BUDDY CLOCK", "WEATHER", "ANALOG"};
static uint8_t s_row = 0, s_prev = 0, s_page = SP_LIST, s_diag_sel = 0;
static uint32_t s_move_t0 = 0;
static float s_sc0 = 0, s_sc1 = 0;
#define ROW_H 40
#define VP0 24
#define VP1 240
#define CONTENT_H (SR_COUNT * ROW_H + 8)

static float scroll_now(void) {
  const float e = ease_out((g_ui.now - s_move_t0) / 140.0f);
  return s_sc0 + (s_sc1 - s_sc0) * e;
}
static float scroll_for(uint8_t r, float cur) {
  const int cy = 4 + r * ROW_H + ROW_H / 2, vh = VP1 - VP0, m = ROW_H + 12;
  float sc = cur;
  if (cy - sc < m) sc = (float)(cy - m);
  if (cy - sc > vh - m) sc = (float)(cy - (vh - m));
  return ui_clampf(sc, 0, (float)(CONTENT_H - vh));
}
static void set_row(int r) {
  if (r < 0) r = 0;
  if (r >= SR_COUNT) r = SR_COUNT - 1;
  if (r == s_row) return;
  const float cur = scroll_now();
  s_prev = s_row;
  s_row = (uint8_t)r;
  s_move_t0 = g_ui.now;
  s_sc0 = cur;
  s_sc1 = scroll_for(s_row, cur);
}
static void jump_row(uint8_t r) {
  s_row = s_prev = r;
  s_move_t0 = g_ui.now - 1000;
  s_sc0 = s_sc1 = scroll_for(r, 0);
}
static void creds_load(void);
static void creds_wipe(void);
static bool s_first_boot = false, s_force_creds = false, s_return_creds = false;
static uint8_t s_editing = 0;
void settings_open_creds(bool first_boot) {
  s_first_boot = first_boot;
  s_force_creds = true;
  if (g_ui.scr != SCR_SETTINGS) ui_go(SCR_SETTINGS);
  else {
    s_force_creds = false;
    s_page = SP_CREDS;
    creds_load();
  }
}
void settings_open_wifi(void) {
  ui_go(SCR_SETTINGS);
  jump_row(SR_WIFI);
  s_page = SP_PORTAL;
  s_return_creds = false;
  hal_portal(true);
}
void settings_open_pckeys(void) {
  ui_go(SCR_SETTINGS);
  jump_row(SR_PCKEYS);
}
void settings_debug_page(uint8_t page, uint8_t row) {
  jump_row(row);
  s_page = page;
  if (page == SP_CREDS) creds_load();
}

static void portal_qr_forget(void);
static void st_enter(uint32_t) {
  s_move_t0 = g_ui.now - 1000;
  if (s_force_creds) {
    s_force_creds = false;
    s_page = SP_CREDS;
    creds_load();
  } else s_page = SP_LIST;
}
static void st_leave(void) {
  if (s_page == SP_PORTAL) hal_portal(false);
  portal_qr_forget();
  creds_wipe();
  s_page = SP_LIST;
  s_first_boot = false;
  s_return_creds = false;
  s_force_creds = false;
  s_editing = 0;
}
static void pckeys_press(void) {
  const BleData &b = g_ui.d.ble;
  if (!g_ui.prefs.pc_keys) {
    g_ui.prefs.pc_keys = 1;
    hal_ble_enable(true);
    hal_ble_kick();
    ui_toast("PC keys on - advertising 2 min", 1800);
  } else if (!b.started) {
    hal_ble_kick();
    ui_toast("Advertising for 2 min", 1500);
  } else {
    g_ui.prefs.pc_keys = 0;
    hal_ble_enable(false);
    ui_toast(b.connected ? "PC keys off - PC disconnected" : "PC keys off", 1800);
  }
  hal_log("[ui] pc_keys=%s", g_ui.prefs.pc_keys ? "ON" : "OFF");
  ui_prefs_changed();
}
static void activate(uint8_t r, int tap_x) {
  UiPrefs &p = g_ui.prefs;
  switch (r) {
    case SR_MENU:
      if (tap_x >= 0) p.menu_list = tap_x >= 238 ? 1 : 0;
      else p.menu_list ^= 1;
      ui_prefs_changed();
      break;
    case SR_FACE: p.face = (uint8_t)((p.face + 1) % 4); ui_prefs_changed(); break;
    case SR_BRIGHT: {
      static const uint8_t lv[5] = {20, 40, 60, 80, 100};
      int i = 0;
      while (i < 5 && lv[i] <= p.bright) i++;
      p.bright = lv[i % 5];
      ui_backlight_refresh();
      ui_prefs_changed();
      break;
    }
    case SR_NIGHT: p.night_dim ^= 1; ui_prefs_changed(); break;
    case SR_TZ:
      p.tz = (uint8_t)((p.tz + 1) % tz_count());
      hal_tz_apply(p.tz);
      ui_prefs_changed();
      break;
    case SR_COMP: s_page = SP_COMP; break;
    case SR_PCKEYS:
      if (tap_x >= 252) {  // tap on the switch itself = strict toggle
        p.pc_keys ^= 1;
        hal_ble_enable(p.pc_keys != 0);
        if (p.pc_keys) hal_ble_kick();
        hal_log("[ui] pc_keys=%s", p.pc_keys ? "ON" : "OFF");
        ui_prefs_changed();
      } else pckeys_press();
      break;
    case SR_CREDS: s_page = SP_CREDS; creds_load(); break;
    case SR_WIFI: s_return_creds = false; s_page = SP_PORTAL; hal_portal(true); break;
    case SR_CAL: ui_go(SCR_CAL); break;
    case SR_DIAG: s_page = SP_DIAG; s_diag_sel = 0; break;
    case SR_ABOUT: s_page = SP_ABOUT; break;
    default: break;
  }
}
static bool is_back(const UiIn &in) { return ui_is_back(in); }
static void comp_toggle(void) {
  g_ui.prefs.comp_on ^= 1;
  hal_comp_enable(g_ui.prefs.comp_on != 0);
  ui_prefs_changed();
  ui_toast(g_ui.prefs.comp_on ? "Companion on - local only" : "Companion off", 1500);
}
static void creds_input(const UiIn &in);
static void portal_close_to(void) {
  hal_portal(false);
  portal_qr_forget();
  if (s_return_creds) {
    s_return_creds = false;
    s_page = SP_CREDS;
    if (g_ui.d.portal.saved) creds_load();
    return;
  }
  s_page = SP_LIST;
}
static void st_input(const UiIn &in, uint32_t) {
  if (s_page == SP_CREDS) { creds_input(in); return; }
  if (s_page != SP_LIST) {
    if (is_back(in)) {
      if (s_page == SP_PORTAL) portal_close_to();
      else s_page = SP_LIST;
      ui_consume();
      return;
    }
    if (s_page == SP_DIAG) {
      if (in.type == IN_ROT) s_diag_sel ^= 1;
      else if (in.type == IN_CLICK || (in.type == IN_TAP && in.y >= 196)) {
        if (in.type == IN_TAP) s_diag_sel = in.x >= 160 ? 1 : 0;
        if (s_diag_sel == 0) ui_go(SCR_TGRID);
        else { hal_snap(); ui_toast("Snapshot sent to serial", 1800); }
        ui_consume();
      }
    } else if (s_page == SP_PORTAL && in.type == IN_CLICK) portal_close_to();
    else if (s_page == SP_ABOUT && in.type == IN_CLICK) s_page = SP_LIST;
    else if (s_page == SP_COMP) {
      if (in.type == IN_CLICK) comp_toggle();
      else if (in.type == IN_TAP && in.y >= 188 && g_ui.d.comp.paired) {
        hal_comp_forget();
        ui_toast("Pairing reset - use the new code", 1800);
        ui_consume();
      }
    }
    return;
  }
  switch (in.type) {
    case IN_ROT: set_row(s_row + in.d); break;
    case IN_CLICK: activate(s_row, -1); break;
    case IN_TAP: {
      if (ui_is_back(in)) break;
      const float sc = scroll_now();
      const int r = (int)((in.y - VP0 + sc - 4) / ROW_H);
      if (r >= 0 && r < SR_COUNT) { jump_row((uint8_t)r); s_sc0 = s_sc1 = sc; activate((uint8_t)r, in.x); ui_consume(); }
      break;
    }
    case IN_SWU: set_row(s_row + 3); ui_consume(); break;
    case IN_SWD: set_row(s_row - 3); ui_consume(); break;
    default: break;
  }
}

// ------------------------------------------------------------------ draw --
static void toggle(int x, int cy, bool on) {
  gfx_rrect(x, cy - 8, 30, 16, 8, on ? blend565(C_BG, C_ACC, 200) : C_TILE_HI);
  gfx_circle(on ? x + 22.0f : x + 8.0f, (float)cy, 6, on ? C_TXT : C_MUT);
}
static void row_value(uint8_t r, int cy, bool f) {
  const UiPrefs &p = g_ui.prefs;
  const uint16_t vc = f ? C_TXT : C_MUT;
  switch (r) {
    case SR_MENU: {
      if (!f) { gfx_micro(296, cy - 5, p.menu_list ? "LIST" : "GRID", vc, GA_R); break; }
      gfx_rrect(178, cy - 15, 122, 30, 8, C_TILE);
      const int sx = p.menu_list ? 238 : 180;
      gfx_rrect(sx, cy - 13, 60, 26, 7, C_TILE_HI);
      gfx_rrect_frame(sx, cy - 13, 60, 26, 7, C_FOCUS_BORDER);
      for (int i = 0; i < 4; i++) gfx_rect(190 + (i & 1) * 5, cy - 5 + (i >> 1) * 5, 4, 4, p.menu_list ? C_MUT : C_TXT);
      gfx_text_m(&AF_LABEL, 203, cy, "Grid", p.menu_list ? C_MUT : C_TXT);
      for (int i = 0; i < 3; i++) gfx_hline(248, cy - 4 + i * 4, 7, p.menu_list ? C_TXT : C_MUT);
      gfx_text_m(&AF_LABEL, 260, cy, "List", p.menu_list ? C_TXT : C_MUT);
      break;
    }
    case SR_FACE: gfx_micro(288, cy - 5, k_face[p.face % 4], vc, GA_R); gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R); break;
    case SR_BRIGHT: {
      char b[8];
      snprintf(b, sizeof(b), "%u%%", (unsigned)p.bright);
      gfx_micro(298, cy - 5, b, vc, GA_R);
      gfx_rrect(186, cy - 2, 76, 4, 2, C_TILE_HI);
      gfx_rrect(186, cy - 2, 76 * p.bright / 100, 4, 2, C_ACC);
      break;
    }
    case SR_NIGHT:
      if (!f) gfx_micro(244, cy - 5, "22:30-06:30", p.night_dim ? vc : C_DIM, GA_R);
      toggle(266, cy, p.night_dim != 0);
      break;
    case SR_TZ:
      gfx_micro(288, cy - 5, tz_label(p.tz), vc, GA_R);
      gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R);
      break;
    case SR_PCKEYS: toggle(266, cy, p.pc_keys != 0); break;
    case SR_COMP: {
      const CompData &c = g_ui.d.comp;
      const char *v = !p.comp_on ? "OFF" : c.paired ? "PAIRED" : "LOCAL";
      gfx_micro(288, cy - 5, v, p.comp_on ? (c.paired ? C_UP : vc) : C_DIM, GA_R);
      gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R);
      break;
    }
    case SR_CREDS: {
      const bool need = g_ui.d.net.wifi_state == WIFI_ST_NO_CREDS;
      gfx_micro(288, cy - 5, need ? "NEEDED" : "READY", need ? C_WARN : vc, GA_R);
      gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R);
      break;
    }
    case SR_WIFI: {
      const AuraData &d = g_ui.d;
      const char *v = d.portal.on ? "PORTAL ON" : d.net.wifi_state == WIFI_ST_UP ? "ONLINE" : d.net.wifi_state == WIFI_ST_NO_CREDS ? "NOT SET UP" : "OFFLINE";
      gfx_micro(288, cy - 5, v, d.net.wifi_state == WIFI_ST_NO_CREDS ? C_WARN : vc, GA_R);
      gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R);
      break;
    }
    case SR_CAL: gfx_micro(288, cy - 5, strcmp(hal_cal_source(), "nvs") == 0 ? "CALIBRATED" : "DEFAULT", vc, GA_R); gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R); break;
    default: gfx_micro(298, cy - 5, AF_CHEV, C_DIM, GA_R); break;
  }
}
static const char *row_sub(uint8_t r, char *b, int n) {
  const UiPrefs &p = g_ui.prefs;
  const BleData &bl = g_ui.d.ble;
  switch (r) {
    case SR_MENU: return "Launcher layout " AF_DOT " default Grid";
    case SR_FACE: return "Press to cycle " AF_DOT " or swipe on Home";
    case SR_BRIGHT: return "Press to step 20-100 %";
    case SR_NIGHT: return "22:30-06:30 local " AF_DOT " Buddy, saver, idle panel";
    case SR_TZ: return "Press to cycle " AF_DOT " clock and night window";
    case SR_COMP:
      if (!p.comp_on) return "Off " AF_DOT " laptop CPU and RAM, this LAN only";
      if (g_ui.d.comp.paired && g_ui.d.comp.ms && g_ui.now - g_ui.d.comp.ms < 15000u) {
        snprintf(b, n, "CPU %u%% " AF_DOT " RAM %u%%", (unsigned)g_ui.d.comp.cpu, (unsigned)g_ui.d.comp.ram);
        return b;
      }
      return g_ui.d.comp.paired ? "Paired " AF_DOT " waiting for the laptop" : "On " AF_DOT " open this row for the code";
    case SR_PCKEYS:
      if (!p.pc_keys) return "Off " AF_DOT " press to turn on";
      if (bl.connected) return "Connected " AF_DOT " press to turn off";
      if (bl.started) {
        snprintf(b, n, "Pairing " AF_DOT " stops in %lu:%02lu", (unsigned long)(bl.adv_left_ms / 60000u), (unsigned long)((bl.adv_left_ms / 1000u) % 60u));
        return b;
      }
      return "Idle " AF_DOT " press to advertise 2 min";
    case SR_CREDS: return "Wi-Fi name, password, and keys";
    case SR_WIFI:
      snprintf(b, n, "Type on your phone " AF_DOT " %s", g_ui.d.oracle.provider ? "PC app" : g_ui.d.oracle.api == ORA_GROQ ? "Groq" : g_ui.d.oracle.api == ORA_GEMINI ? "Gemini" : "xAI");
      return b;
    case SR_CAL: return "4 targets " AF_DOT " about 20 s";
    case SR_DIAG: return "Stats, tap-grid test, snapshot";
    default: return "Version and credits";
  }
}
static void list_draw(void) {
  const float sc = scroll_now();
  const float e = ease_out((g_ui.now - s_move_t0) / 140.0f);
  gfx_clip(0, VP0, GFX_W, VP1);
  const float gcy = VP0 + 4 + (s_prev + (s_row - s_prev) * e) * ROW_H + ROW_H / 2 - sc;
  if (gfx_vis((int)gcy - 24, 48)) {
    gfx_tile(8, (int)gcy - 23, 297, 46, 10, C_TILE_HI_TOP, C_TILE_HI, blend565(C_TILE_HI_TOP, C_TXT, 30));
    gfx_rrect_frame(8, (int)gcy - 23, 297, 46, 10, C_FOCUS_BORDER);
    gfx_rrect(12, (int)gcy - 11, 3, 22, 1, C_ACC);
  }
  for (int r = 0; r < SR_COUNT; r++) {
    const int cy = VP0 + 4 + r * ROW_H + ROW_H / 2 - (int)sc;
    if (!gfx_vis(cy - ROW_H / 2, ROW_H)) continue;
    const bool f = r == s_row;
    if (f) {
      char b[48];
      const int maxw = r == SR_MENU ? 120 : (r == SR_PCKEYS || r == SR_NIGHT ? 186 : 200);
      w_text_ell(&AF_ROW_FOCUS, 24, cy - 7, k_rn[r], maxw + 40, C_TXT);
      w_text_ell(&AF_BODY_S, 24, cy + 10, row_sub((uint8_t)r, b, sizeof(b)), r == SR_MENU ? 150 : (r == SR_BRIGHT ? 156 : 228), C_MUT);
    } else {
      gfx_text_m(&AF_ROW, 24, cy, k_rn[r], blend565(C_BG, C_MUT, 235));
      if (r + 1 < SR_COUNT && r + 1 != s_row) gfx_hline(24, cy + ROW_H / 2, 276, blend565(C_BG, C_LINE, 160));
    }
    row_value((uint8_t)r, cy, f);
  }
  for (int k = 0; k < 6; k++) gfx_rect_a(0, VP0 + k, 310, 1, C_BG, (uint8_t)(255 - k * 42));
  gfx_noclip();
  if (gfx_vis(30, 204)) {
    gfx_rrect(313, 30, 3, 204, 1, blend565(C_BG, C_LINE, 150));
    const int tl = 204 * (VP1 - VP0) / CONTENT_H;
    const int ty = 30 + (int)((204 - tl) * (sc / (CONTENT_H - (VP1 - VP0))));
    gfx_rrect(312, ty, 5, tl, 2, C_ACC);
  }
}
// Join QR (Q01): encoded once per SSID/password pair (FNV-1a of the pair, the
// password itself is not copied), drawn as dark modules on a white quiet zone.
static uint8_t s_qr[WQR_BUF_LEN];
static int s_qr_side = 0;
static uint32_t s_qr_key = 0;
static uint32_t fnv(uint32_t h, const char *s) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h * 16777619u;
}
static void portal_qr_forget(void) {  // the modules encode the AP password: volatile wipe, not memset
  volatile uint8_t *v = s_qr;
  for (size_t i = 0; i < sizeof(s_qr); i++) v[i] = 0;
  s_qr_side = 0;
  s_qr_key = 0;
}
static int portal_qr(const PortalData &p) {
  const uint32_t k = fnv(fnv(2166136261u, p.ap_ssid), p.ap_pass);
  if (k != s_qr_key) {
    s_qr_key = k;
    s_qr_side = wifi_qr_encode(p.ap_ssid, p.ap_pass, s_qr);
    hal_log("[portal] join qr %dx%d", s_qr_side, s_qr_side);
  }
  return s_qr_side;
}
static void portal_draw(void) {
  const AuraData &d = g_ui.d;
  const PortalData &p = d.portal;
  w_card(10, 30, 300, 150, 10);
  if (p.on && !p.saved && p.ap_ssid[0] && p.ap_pass[0]) {
    const int side = portal_qr(p);
    const int m = side && side <= 29 ? 3 : 2, q = 4;  // module px, quiet zone (modules)
    const int bw = side ? (side + 2 * q) * m : 110;
    const int bx = 74 - bw / 2, by = 105 - bw / 2;
    if (gfx_vis(by, bw)) {
      gfx_rrect(bx, by, bw, bw, 4, 0xFFFF);
      for (int y = 0; y < side; y++) {
        const int py = by + (q + y) * m;
        if (!gfx_vis(py, m)) continue;
        for (int x = 0; x < side;) {  // horizontal runs of dark modules
          if (!wifi_qr_module(s_qr, x, y)) { x++; continue; }
          int e = x + 1;
          while (e < side && wifi_qr_module(s_qr, e, y)) e++;
          gfx_rect(bx + (q + x) * m, py, (e - x) * m, m, 0x0000);
          x = e;
        }
      }
      if (!side) gfx_micro(bx + bw / 2, by + bw / 2 - 4, "NO QR", C_DN, GA_C, true);
    }
    const int tx = 140;
    gfx_micro(tx, 38, "1. SCAN, OR JOIN WI-FI", C_DIM, GA_L, true);
    w_text_ell(&AF_LABEL_M, tx, 56, p.ap_ssid, 164, C_TXT);
    gfx_micro(tx, 72, "PASSWORD", C_DIM, GA_L, true);
    gfx_text_m(&AF_LABEL_M, tx, 89, p.ap_pass, C_WARN);
    gfx_micro(tx, 104, "2. THEN OPEN THE SETUP PAGE", C_DIM, GA_L, true);
    gfx_text_m(&AF_LABEL_M, tx, 121, "http://192.168.4.1", C_ACC);
    char b[40];
    snprintf(b, sizeof(b), "%u PHONE%s CONNECTED", (unsigned)p.clients, p.clients == 1 ? "" : "S");
    gfx_micro(tx, 136, b, p.clients ? C_UP : C_MUT, GA_L, true);
    gfx_hline(tx, 148, 160, C_LINE);
    gfx_micro(tx, 155, "WI-FI " AF_DOT " KEYS " AF_DOT " LOCATION", C_MUT, GA_L, true);
    gfx_micro(tx, 166, "KEYS ARE NEVER SHOWN", C_DIM, GA_L, true);
  } else {
    gfx_a4(24, 52, 48, 48, state_glyph48(SG_PHONE_WIFI), C_ACC);
    if (p.saved) {
      gfx_text_m(&AF_TITLE, 88, 58, "Saved", C_UP);
      gfx_text_m(&AF_BODY_S, 88, 80, "Setup page closed. Connecting to", C_MUT);
      gfx_text_m(&AF_BODY_S, 88, 96, "your Wi-Fi with the new settings.", C_MUT);
    } else {
      gfx_text_m(&AF_TITLE, 88, 58, "Starting " AF_ELL, C_TXT);
      w_spinner(276, 60, 9, C_ACC, C_TILE);
    }
    gfx_hline(24, 142, 272, C_LINE);
    gfx_micro(24, 152, "SSID " AF_DOT " PASSWORD " AF_DOT " API KEYS " AF_DOT " LOCATION", C_MUT, GA_L, true);
    gfx_micro(24, 164, "SAVED ON THIS DEVICE " AF_DOT " NEVER DISPLAYED", C_DIM, GA_L, true);
  }
  w_footer("PRESS / HOLD: CLOSE SETUP");
}
// True while the per-session AP password (text or QR) can be on screen: the
// serial snapshot is refused then (portal password on screen).
bool ui_portal_secret_visible(void) {
  return g_ui.scr == SCR_SETTINGS && (s_page == SP_PORTAL || s_page == SP_CREDS);
}
bool ui_secret_on_screen(void) {
  return ui_portal_secret_visible() || (g_ui.scr == SCR_SETTINGS && s_page == SP_COMP && g_ui.prefs.comp_on) ||
         pc_token_visible();
}
static void diag_line(int y, const char *k, const char *v, uint16_t c = C_TXT) {
  gfx_micro(20, y, k, C_DIM, GA_L, true);
  gfx_micro(150, y, v, c, GA_R, true);
}
static void diag_draw(void) {
  HalDiag h;
  hal_diag(&h);
  const AuraData &d = g_ui.d;
  w_card(10, 28, 146, 164, 8);
  w_card(164, 28, 146, 164, 8);
  if (gfx_vis(28, 164)) {
    char b[32];
    int y = 36;
    snprintf(b, sizeof(b), "%u.%u", (unsigned)(h.fps_x10 / 10), (unsigned)(h.fps_x10 % 10)); diag_line(y, "FPS", b); y += 13;
    snprintf(b, sizeof(b), "%lu MS", (unsigned long)h.worst_ms); diag_line(y, "WORST FRAME", b, h.worst_ms > 40 ? C_WARN : C_TXT); y += 13;
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.overruns); diag_line(y, "OVERRUNS", b); y += 13;
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)h.band_fail, (unsigned long)h.touch_bus); diag_line(y, "BAND / BUS", b); y += 13;
    snprintf(b, sizeof(b), "%lu MS", (unsigned long)h.lat_max); diag_line(y, "LAT MAX", b); y += 13;
    snprintf(b, sizeof(b), "%luK", (unsigned long)(hal_heap_free() / 1024)); diag_line(y, "HEAP FREE", b); y += 13;
    snprintf(b, sizeof(b), "%luK", (unsigned long)(hal_heap_largest() / 1024)); diag_line(y, "LARGEST", b, hal_heap_largest() < 20480 ? C_WARN : C_TXT); y += 13;
    snprintf(b, sizeof(b), "%luK", (unsigned long)(hal_heap_min() / 1024)); diag_line(y, "HEAP MIN", b); y += 13;
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)d.net.fetch_ok, (unsigned long)d.net.fetch_fail); diag_line(y, "FETCH OK/FAIL", b); y += 13;
    snprintf(b, sizeof(b), "%lu", (unsigned long)d.net.tls_skipped); diag_line(y, "TLS SKIPPED", b); y += 13;
    snprintf(b, sizeof(b), "%luK", (unsigned long)(d.net.tls_largest_min / 1024)); diag_line(y, "TLS LARGEST MIN", b);
    // touch column
    y = 36;
    const int xo = 154;
    auto line2 = [&](const char *k, const char *v) { gfx_micro(20 + xo, y, k, C_DIM, GA_L, true); gfx_micro(150 + xo, y, v, C_TXT, GA_R, true); y += 13; };
    snprintf(b, sizeof(b), "%s", hal_cal_source()); line2("CAL", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_reads); line2("READS", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_rej_z); line2("REJ PRESSURE", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_rej_range); line2("REJ RANGE", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_rej_spread); line2("REJ SPREAD", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_rej_jump); line2("REJ JUMP", b);
    snprintf(b, sizeof(b), "%lu", (unsigned long)h.t_fallback); line2("FALLBACK", b);
    snprintf(b, sizeof(b), "%s", h.irq ? h.irq : "-"); line2("TOUCH IRQ", b);
    snprintf(b, sizeof(b), "%ld", (long)h.enc_raw); line2("ENCODER", b);
    snprintf(b, sizeof(b), "%s", !d.ble.enabled ? "OFF" : d.ble.connected ? "CONNECTED" : d.ble.started ? "ADVERTISING" : "IDLE"); line2("BLE", b);
    snprintf(b, sizeof(b), "%lu / %lu", (unsigned long)d.ble.hid_sent, (unsigned long)d.ble.hid_dropped); line2("HID SENT/DROP", b);
  }
  static const char *const act[2] = {"Tap-grid test", "Snapshot (serial)"};
  for (int i = 0; i < 2; i++) {
    const int x = 10 + i * 154;
    w_card(x, 198, 146, 26, 8, s_diag_sel == i, s_diag_sel == i);
    gfx_text_m(&AF_LABEL, x + 73, 211, act[i], C_TXT, GA_C);
  }
  w_footer("TURN: SELECT   PRESS: RUN   HOLD: BACK");
}
static void about_draw(void) {
  w_card(10, 30, 300, 150, 10);
  gfx_text_m(&AF_NUM_L, 160, 62, AURA_NAME, C_TXT, GA_C);
  gfx_micro(160, 82, FW_BY, C_MUT, GA_C);
  gfx_micro(160, 104, "FIRMWARE " AURA_VERSION " " AF_DOT " ESP32-WROOM-32", C_TXT, GA_C, true);
  char b[64];
  const uint32_t up = hal_uptime_s();
  snprintf(b, sizeof(b), "UPTIME %luH %02luM " AF_DOT " HEAP %luK", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60), (unsigned long)(hal_heap_free() / 1024));
  gfx_micro(160, 120, b, C_DIM, GA_C, true);
  gfx_micro(160, 142, "DATA: OPEN-METEO " AF_DOT " COINGECKO " AF_DOT " BBC RSS", C_DIM, GA_C, true);
  gfx_micro(160, 156, "FONT: POPPINS (SIL OFL 1.1)", C_DIM, GA_C, true);
  w_footer("PRESS / HOLD: BACK");
}
static void comp_draw(void) {
  const CompData &c = g_ui.d.comp;
  const bool on = g_ui.prefs.comp_on != 0;
  w_card(10, 30, 300, 150, 10);
  gfx_micro(24, 40, "THIS WI-FI ONLY " AF_DOT " NOTHING IS UPLOADED", C_DIM, GA_L, true);
  if (!on) {
    gfx_text_m(&AF_TITLE, 24, 68, "Companion off", C_TXT);
    gfx_text_m(&AF_BODY_S, 24, 92, "Press to listen for one laptop.", C_MUT);
    gfx_text_m(&AF_BODY_S, 24, 110, "It may send CPU, RAM and a short title.", C_MUT);
  } else {
    gfx_micro(24, 58, "PAIRING CODE", C_DIM, GA_L, true);
    gfx_text_m(&AF_TITLE, 24, 78, c.token[0] ? c.token : "--------", C_ACC);
    char b[48];
    if (c.listening && c.ip) {
      const uint32_t ip = c.ip;
      snprintf(b, sizeof(b), "%u.%u.%u.%u:%u", (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255),
               (unsigned)((ip >> 16) & 255), (unsigned)(ip >> 24), (unsigned)COMP_PORT);
    } else if (g_ui.d.net.wifi_state == WIFI_ST_UP) snprintf(b, sizeof(b), "STARTING");
    else snprintf(b, sizeof(b), "WAITING FOR WI-FI");
    gfx_micro(24, 96, b, C_MUT, GA_L, true);
    const uint8_t cpu = c.ms ? c.cpu : 0, ram = c.ms ? c.ram : 0;
    gfx_micro(24, 114, "CPU", C_DIM, GA_L, true);
    gfx_rrect(52, 116, 180, 6, 2, C_TILE_HI);
    if (cpu) gfx_rrect(52, 116, 180 * cpu / 100, 6, 2, C_ACC);
    gfx_micro(24, 130, "RAM", C_DIM, GA_L, true);
    gfx_rrect(52, 132, 180, 6, 2, C_TILE_HI);
    if (ram) gfx_rrect(52, 132, 180 * ram / 100, 6, 2, C_ACC);
    if (c.note[0] && c.ms && g_ui.now - c.ms < 20000u) w_text_ell(&AF_BODY_S, 24, 152, c.note, 272, C_TXT);
    else gfx_micro(24, 148, c.paired ? "PAIRED " AF_DOT " NO RECENT SAMPLE" : "NOT PAIRED YET", C_DIM, GA_L, true);
  }
  if (on && c.paired) w_footer("PRESS: OFF   TAP BOTTOM: FORGET   HOLD: BACK");
  else w_footer(on ? "PRESS: TURN OFF   HOLD: BACK" : "PRESS: TURN ON   HOLD: BACK");
}
static void creds_draw(void);
static const char *creds_title(void);
static void st_draw(void) {
  gfx_fill(C_BG);
  switch (s_page) {
    case SP_PORTAL: portal_draw(); w_status("WI-FI SETUP", ""); return;
    case SP_DIAG: diag_draw(); w_status("DIAGNOSTICS", ""); return;
    case SP_ABOUT: about_draw(); w_status("ABOUT", ""); return;
    case SP_COMP: comp_draw(); w_status("COMPANION", ""); return;
    case SP_CREDS: creds_draw(); w_status(creds_title(), ""); return;
    default: list_draw(); w_status("SETTINGS", ""); return;
  }
}
// Portal and the companion code page. Idle takeover would call st_leave().
// The portal leave stops the AP (hal_portal(false)). The companion listener
// stays up when you leave the page; this lock only keeps the code on screen.
static bool st_idle_lock(void) { return s_page == SP_PORTAL || s_page == SP_COMP || s_page == SP_CREDS || s_first_boot; }
const Screen SCR_SETTINGS_DEF = {"SETTINGS", st_enter, st_leave, st_input, nullptr, st_draw, true, false, nullptr, st_idle_lock};

// ============================================================ credentials ==
enum { CF_SSID = 0, CF_PASS, CF_OWM, CF_CRICKET, CF_STOCK, CF_GROQ, CF_XAI, CF_GEMINI, CF_N };
enum { CF_PHONE = CF_N, CF_ROWS };
enum { COP_KEEP = 0, COP_SET = 1, COP_CLEAR = 2 };
enum { KK_CHARS = 32, KK_DONE = 32, KK_SPACE = 33, KK_DEL = 34, KK_CASE = 35, KK_SAVE = 36, KK_N = 37 };
#define CRED_ROW_H 40
#define CRED_LIST_Y0 40
#define CRED_LIST_Y1 200
static const char *const k_flab[CF_N] = {"Wi-Fi name", "Wi-Fi password", "Weather API key", "Cricket",
                                        "Stocks", "Groq / Oracle", "xAI", "Gemini"};
static const char *const k_fhint[CF_N] = {"Name of your Wi-Fi", "Dots hide it. Empty means no password.",
                                         "From openweathermap.org", "For match scores. Optional.",
                                         "For share prices. Optional.", "Lets AURA answer questions",
                                         "Optional", "Optional"};
static const uint8_t k_fmax[CF_N] = {32, 64, 128, 128, 128, 128, 128, 128};
static const bool k_fmask[CF_N] = {false, true, true, true, true, true, true, true};
static const int8_t k_fnet[CF_N] = {-1, -1, 4, 1, 0, 3, 2, 5};  // stocks, cricket, oracle, groq, owm, gemini
static const char *const k_lay[3][4] = {
    {"qwertyui", "opasdfgh", "jklzxcvb", "nm.-_@/?"},
    {"QWERTYUI", "OPASDFGH", "JKLZXCVB", "NM.-_@/?"},
    {"12345678", "90@#*$%&", "+=()[]{}", ".,:;'\"\\?"},
};
static char s_ssid[33], s_pass[65], s_keys[6][129], s_loaded_ssid[33];
static uint8_t s_ssid_n = 0, s_pass_n = 0, s_kn[6], s_op[CF_N], s_saved[CF_N];
static uint8_t s_field = 0, s_kcase = 0, s_list_row = 0, s_list_scroll = 0;
static int s_ki = 0;
static void cred_wipe_mem(void *p, size_t n) {
  volatile uint8_t *v = (volatile uint8_t *)p;
  while (n--) *v++ = 0;
}
static char *fld_buf(int f, uint8_t **np) {
  if (f == CF_SSID) { if (np) *np = &s_ssid_n; return s_ssid; }
  if (f == CF_PASS) { if (np) *np = &s_pass_n; return s_pass; }
  const int k = f - 2;
  if (np) *np = &s_kn[k];
  return s_keys[k];
}
static void creds_wipe(void) {
  cred_wipe_mem(s_ssid, sizeof(s_ssid));
  cred_wipe_mem(s_pass, sizeof(s_pass));
  cred_wipe_mem(s_keys, sizeof(s_keys));
  cred_wipe_mem(s_loaded_ssid, sizeof(s_loaded_ssid));
  s_ssid_n = s_pass_n = 0;
  memset(s_kn, 0, sizeof(s_kn));
  memset(s_op, 0, sizeof(s_op));
  memset(s_saved, 0, sizeof(s_saved));
}
static void creds_load(void) {
  creds_wipe();
  s_field = s_ki = s_kcase = s_editing = s_list_row = s_list_scroll = 0;
  HalCredQuery q;
  hal_cred_query(&q);
  memcpy(s_ssid, q.ssid, sizeof(s_ssid) - 1);
  s_ssid[sizeof(s_ssid) - 1] = 0;
  s_ssid_n = (uint8_t)strlen(s_ssid);
  memcpy(s_loaded_ssid, s_ssid, sizeof(s_loaded_ssid));
  s_saved[CF_SSID] = s_ssid_n;
  s_saved[CF_PASS] = q.pass_len;
  for (int i = 2; i < CF_N; i++) s_saved[i] = q.key_len[k_fnet[i]];
}
static void trim_copy(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  size_t i = 0;
  while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, strlen(s + i) + 1);
}
static void creds_insert(char ch) {
  if (ch < 0x20 || ch > 0x7e) return;
  uint8_t *np = nullptr;
  char *b = fld_buf(s_field, &np);
  if (*np >= k_fmax[s_field]) return;
  s_op[s_field] = COP_SET;
  b[(*np)++] = ch;
  b[*np] = 0;
}
static void creds_bs(void) {
  uint8_t *np = nullptr;
  char *b = fld_buf(s_field, &np);
  if (!*np) return;
  b[--(*np)] = 0;
  s_op[s_field] = COP_SET;
}
static void creds_clear_field(void) {
  uint8_t *np = nullptr;
  char *b = fld_buf(s_field, &np);
  cred_wipe_mem(b, (size_t)k_fmax[s_field] + 1);
  *np = 0;
  s_op[s_field] = s_saved[s_field] ? COP_CLEAR : COP_KEEP;
}
static void creds_later(void) {
  s_first_boot = false;
  ui_toast("You can finish this in Settings", 1800);
  ui_go(SCR_HOME);
}
static void creds_save(void) {
  char ssid[33], loaded[33];
  memcpy(ssid, s_ssid, sizeof(ssid));
  memcpy(loaded, s_loaded_ssid, sizeof(loaded));
  ssid[32] = loaded[32] = 0;
  trim_copy(ssid);
  trim_copy(loaded);
  const bool ssid_changed = s_op[CF_SSID] == COP_CLEAR || strcmp(ssid, loaded) != 0;
  const bool pass_set = s_op[CF_PASS] == COP_SET;
  const bool pass_clear = s_op[CF_PASS] == COP_CLEAR;
  bool write_wifi = false;
  const char *pass = "";
  if (s_op[CF_SSID] == COP_CLEAR || !ssid[0]) {
    if (s_first_boot || ssid_changed) {
      ui_toast("Type your Wi-Fi name first", 1800);
      s_field = s_list_row = CF_SSID;
      s_editing = 1;
      return;
    }
  } else if (!ssid_changed && !pass_set && !pass_clear) {
    write_wifi = false;
  } else if (ssid_changed && !pass_set && !pass_clear && s_saved[CF_PASS]) {
    ui_toast("Type the password, or tap X if none", 2200);
    s_field = s_list_row = CF_PASS;
    s_editing = 1;
    return;
  } else {
    pass = pass_set ? s_pass : "";
    const size_t lp = strlen(pass);
    if (lp > 0 && lp < 8) {
      ui_toast("Use 8 or more characters, or none", 2200);
      s_field = s_list_row = CF_PASS;
      s_editing = 1;
      return;
    }
    write_wifi = true;
  }
  HalCredCommit c = {};
  c.ssid = ssid;
  c.pass = pass;
  c.write_wifi = write_wifi;
  bool any = write_wifi;
  for (int i = 2; i < CF_N; i++) {
    const int net = k_fnet[i];
    if (s_op[i] == COP_SET && s_kn[i - 2] > 0) {
      c.key_op[net] = 1;
      c.key[net] = s_keys[i - 2];
      any = true;
    } else if (s_op[i] == COP_CLEAR) {
      c.key_op[net] = 2;
      any = true;
    }
  }
  if (!any) {
    if (s_first_boot && hal_nvs_has_ssid()) {
      ui_toast("Saved", 1200);
      s_first_boot = false;
      ui_go(SCR_HOME);
      return;
    }
    ui_toast(s_first_boot ? "Type your Wi-Fi name first" : "Nothing new to save", 1600);
    if (s_first_boot) { s_field = s_list_row = CF_SSID; s_editing = 1; }
    return;
  }
  const int rc = hal_cred_commit(&c);
  if (rc == 1) { ui_toast("Wi-Fi was not saved. Check it.", 2200); return; }
  if (rc != 0) { ui_toast("Could not save. Try again.", 2000); return; }
  ui_toast("Saved", 1400);
  const bool go_home = s_first_boot;
  s_first_boot = false;
  if (go_home) ui_go(SCR_HOME);
  else { creds_wipe(); s_page = SP_LIST; }
}
static char creds_key_char(int i) {
  if (i < 0 || i >= KK_CHARS) return 0;
  const char *row = k_lay[s_kcase % 3][i / 8];
  const int col = i % 8;
  return (int)strlen(row) > col ? row[col] : 0;
}
static int creds_wrap(int i, int d, int n) {
  int x = i + d;
  x %= n;
  if (x < 0) x += n;
  return x;
}
static void cred_see_row(void) {
  const int vis = (CRED_LIST_Y1 - CRED_LIST_Y0) / CRED_ROW_H;
  if (s_list_row < s_list_scroll) s_list_scroll = s_list_row;
  if (s_list_row >= s_list_scroll + vis) s_list_scroll = (uint8_t)(s_list_row - vis + 1);
}
static void kb_rect(int i, int *x, int *y, int *w, int *h) {
  if (i < KK_CHARS) {
    *w = 36; *h = 30;
    *x = 4 + (i % 8) * 39;
    *y = 78 + (i / 8) * 32;
    return;
  }
  *y = 206; *h = 30;
  switch (i) {
    case KK_DONE: *x = 4; *w = 58; break;
    case KK_SPACE: *x = 66; *w = 78; break;
    case KK_DEL: *x = 148; *w = 46; break;
    case KK_CASE: *x = 198; *w = 46; break;
    default: *x = 248; *w = 68; break;
  }
}
static const char *creds_key_label(int i, char *tmp) {
  if (i < KK_CHARS) {
    tmp[0] = creds_key_char(i);
    tmp[1] = 0;
    return tmp[0] ? tmp : "";
  }
  switch (i) {
    case KK_CASE: return s_kcase == 0 ? "abc" : s_kcase == 1 ? "ABC" : "123";
    case KK_SPACE: return "space";
    case KK_DEL: return "del";
    case KK_DONE: return "Done";
    case KK_SAVE: return "Save";
    default: return "";
  }
}
static void creds_open_phone(void) {
  s_return_creds = true;
  s_page = SP_PORTAL;
  hal_portal(true);
}
static void creds_activate(int i) {
  if (i < 0 || i >= KK_N) return;
  s_ki = i;
  if (i < KK_CHARS) {
    const char ch = creds_key_char(i);
    if (ch) creds_insert(ch);
    return;
  }
  switch (i) {
    case KK_CASE: s_kcase = (uint8_t)((s_kcase + 1) % 3); break;
    case KK_SPACE: creds_insert(' '); break;
    case KK_DEL: creds_bs(); break;
    case KK_DONE: s_editing = 0; break;
    case KK_SAVE: creds_save(); break;
    default: break;
  }
}
static bool creds_hit(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}
static void creds_tap_list(int x, int y) {
  if (y >= 202) {
    if (x < 160) {
      if (s_first_boot) creds_later();
      else { creds_wipe(); s_page = SP_LIST; }
    } else creds_save();
    return;
  }
  if (y < CRED_LIST_Y0) return;
  const int r = s_list_scroll + (y - CRED_LIST_Y0) / CRED_ROW_H;
  if (r < 0 || r >= CF_ROWS) return;
  s_list_row = (uint8_t)r;
  if (r == CF_PHONE) creds_open_phone();
  else { s_field = (uint8_t)r; s_editing = 1; s_ki = 0; }
}
static void creds_tap_edit(int x, int y) {
  if (creds_hit(x, y, 276, 38, 40, 34)) { creds_clear_field(); return; }
  for (int i = 0; i < KK_N; i++) {
    int rx, ry, rw, rh;
    kb_rect(i, &rx, &ry, &rw, &rh);
    if (creds_hit(x, y, rx, ry, rw, rh)) { creds_activate(i); return; }
  }
}
static void creds_input(const UiIn &in) {
  if (is_back(in)) {
    if (s_editing) s_editing = 0;
    else if (s_first_boot) ui_toast("Tap Save, or Later", 1600);
    else { creds_wipe(); s_page = SP_LIST; }
    ui_consume();
    return;
  }
  if (!s_editing) {
    switch (in.type) {
      case IN_ROT:
        s_list_row = (uint8_t)creds_wrap(s_list_row, in.d, CF_ROWS);
        cred_see_row();
        break;
      case IN_SWD:
        s_list_row = (uint8_t)creds_wrap(s_list_row, -1, CF_ROWS);
        cred_see_row();
        ui_consume();
        break;
      case IN_SWU:
        s_list_row = (uint8_t)creds_wrap(s_list_row, 1, CF_ROWS);
        cred_see_row();
        ui_consume();
        break;
      case IN_CLICK:
        if (s_list_row == CF_PHONE) creds_open_phone();
        else { s_field = s_list_row; s_editing = 1; s_ki = 0; }
        ui_consume();
        break;
      case IN_TAP: creds_tap_list(in.x, in.y); ui_consume(); break;
      default: break;
    }
    return;
  }
  switch (in.type) {
    case IN_ROT: s_ki = creds_wrap(s_ki, in.d, KK_N); break;
    case IN_CLICK: creds_activate(s_ki); ui_consume(); break;
    case IN_SWL: s_field = s_list_row = (uint8_t)creds_wrap(s_field, 1, CF_N); ui_consume(); break;
    case IN_SWR: s_field = s_list_row = (uint8_t)creds_wrap(s_field, -1, CF_N); ui_consume(); break;
    case IN_TAP: creds_tap_edit(in.x, in.y); ui_consume(); break;
    default: break;
  }
}
static void creds_preview(char *out, int cap) {
  uint8_t *np = nullptr;
  char *b = fld_buf(s_field, &np);
  if (s_op[s_field] == COP_CLEAR) { snprintf(out, cap, "Will be removed"); return; }
  if (*np == 0 && s_saved[s_field]) { snprintf(out, cap, "Saved"); return; }
  if (*np == 0) {
    snprintf(out, cap, "%s", s_field == CF_SSID ? "Type the Wi-Fi name" : s_field == CF_PASS ? "Leave empty if none" : "Not added");
    return;
  }
  if (k_fmask[s_field]) {
    const int n = *np > 18 ? 18 : *np;
    for (int i = 0; i < n; i++) out[i] = '*';
    out[n] = 0;
    return;
  }
  int keep = (int)strlen(b);
  if (keep > cap - 1) keep = cap - 1;
  memcpy(out, b, (size_t)keep);
  out[keep] = 0;
}
static void cred_row_status(int f, char *out, int cap) {
  if (f == CF_PHONE) { snprintf(out, cap, "Open"); return; }
  uint8_t *np = nullptr;
  char *b = fld_buf(f, &np);
  if (s_op[f] == COP_CLEAR) { snprintf(out, cap, "Removing"); return; }
  if (f == CF_SSID && *np) {
    int keep = *np > 14 ? 14 : *np;
    memcpy(out, b, (size_t)keep);
    out[keep] = 0;
    return;
  }
  if (s_op[f] == COP_SET && *np) { snprintf(out, cap, "Hidden"); return; }
  if (s_saved[f]) { snprintf(out, cap, "Saved"); return; }
  snprintf(out, cap, "%s", f == CF_SSID ? "Needed" : "Not set");
}
static const char *creds_title(void) {
  if (s_editing && s_field < CF_N) return k_flab[s_field];
  return s_first_boot ? "SET UP" : "ACCOUNTS";
}
static void creds_draw_list(void) {
  cred_see_row();
  gfx_micro(12, 28, s_first_boot ? "TAP WI-FI NAME, THEN SAVE" : "TAP A ROW, THEN SAVE", C_DIM);
  for (int i = 0; i < CF_ROWS; i++) {
    const int y = CRED_LIST_Y0 + (i - s_list_scroll) * CRED_ROW_H;
    if (y + CRED_ROW_H <= CRED_LIST_Y0 || y >= CRED_LIST_Y1) continue;
    if (!gfx_vis(y, CRED_ROW_H - 4)) continue;
    const bool on = i == s_list_row;
    w_card(8, y, 304, CRED_ROW_H - 6, 8, on, on);
    char st[20];
    cred_row_status(i, st, sizeof(st));
    const char *lab = i == CF_PHONE ? "Use your phone" : k_flab[i];
    const int maxw = (i == CF_XAI || i == CF_GEMINI) ? 168 : 190;
    w_text_ell(&AF_ROW, 18, y + 16, lab, maxw, C_TXT);
    if (i == CF_XAI || i == CF_GEMINI) gfx_micro(168, y + 8, "OPTIONAL", C_DIM);
    gfx_micro(300, y + 10, st, on ? C_ACC : C_MUT, GA_R, true);
  }
  const int yb = 204;
  w_card(8, yb, 148, 32, 8, false, false);
  w_card(164, yb, 148, 32, 8, true, true);
  gfx_text_m(&AF_LABEL, 82, yb + 16, s_first_boot ? "Later" : "Back", C_TXT, GA_C);
  gfx_text_m(&AF_LABEL, 238, yb + 16, "Save", C_TXT, GA_C);
}
static void creds_draw_edit(void) {
  char b[48], lab[4];
  gfx_micro(12, 26, k_fhint[s_field], C_DIM);
  w_card(8, 38, 262, 34, 8);
  creds_preview(b, sizeof(b));
  uint8_t *np = nullptr;
  fld_buf(s_field, &np);
  const bool hint = s_op[s_field] == COP_CLEAR || *np == 0;
  const uint16_t vc = s_op[s_field] == COP_CLEAR ? C_WARN : hint ? C_MUT : C_TXT;
  w_text_ell(&AF_LABEL, 18, 55, b, 236, vc);
  gfx_rrect(276, 38, 40, 34, 8, C_TILE_HI);
  gfx_rrect_frame(276, 38, 40, 34, 8, C_WARN);
  gfx_text_m(&AF_TITLE, 296, 55, "X", C_WARN, GA_C);
  for (int i = 0; i < KK_N; i++) {
    int x, y, w, h;
    kb_rect(i, &x, &y, &w, &h);
    if (!gfx_vis(y, h)) continue;
    const bool f = i == s_ki;
    gfx_rrect(x, y, w, h, 6, f ? C_TILE_HI : C_TILE);
    if (f || i == KK_SAVE) gfx_rrect_frame(x, y, w, h, 6, i == KK_SAVE ? C_ACC : C_FOCUS_BORDER);
    const char *t = creds_key_label(i, lab);
    gfx_text_m(i < KK_CHARS ? &AF_LABEL : &AF_MICRO, x + w / 2, y + h / 2, t, f || i == KK_SAVE ? C_TXT : C_MUT, GA_C);
  }
}
static void creds_draw(void) {
  if (s_editing) creds_draw_edit();
  else creds_draw_list();
}

// ============================================================ calibration ==
// 3 solve targets + 1 verify target, 16 raw samples each (after 3 settle reads).
static const int16_t k_cal_pts[4][2] = {{30, 30}, {290, 120}, {90, 210}, {230, 200}};
enum CalPh : uint8_t { CP_WAIT_UP = 0, CP_WAIT_DOWN, CP_SAMPLING, CP_DONE };
static uint8_t s_ci = 0, s_cph = CP_WAIT_UP, s_cskip = 0, s_cattempt = 0;
static uint32_t s_cax = 0, s_cay = 0, s_cn = 0, s_ct0 = 0;
static int32_t s_craw[4][2];
static void cal_restart(uint32_t now) {
  s_ci = 0;
  s_cph = CP_WAIT_UP;
  s_ct0 = now;
  hal_log("[cal] start (attempt %u)", (unsigned)(s_cattempt + 1));
}
static void cal_enter(uint32_t now) {
  s_cattempt = 0;
  hal_cal_restore_jump_reject(false);
  cal_restart(now);
}
static void cal_leave(void) { hal_cal_restore_jump_reject(true); }
static void cal_finish(uint32_t now) {
  int32_t err = -1;
  const bool ok = hal_cal_solve_save((const int32_t(*)[2])s_craw, k_cal_pts, &err);
  if (ok) {
    char b[40];
    snprintf(b, sizeof(b), "Calibrated (%ld px)", (long)err);
    ui_toast(b, 2500);
    s_cph = CP_DONE;
    ui_go(SCR_SETTINGS);
  } else if (++s_cattempt < 3) {
    ui_toast("Missed - let's try again", 2000);
    cal_restart(now);
  } else {
    ui_toast("Calibration failed, kept old", 2500);
    ui_go(SCR_SETTINGS);
  }
}
void cal_on_raw(const UiTouchRaw &r, uint32_t now) {
  if (g_ui.scr != SCR_CAL || s_cph == CP_DONE) return;
  if (now - s_ct0 > 60000u) {
    hal_log("[cal] timeout, keeping previous calibration");
    ui_toast("Calibration timed out", 2000);
    ui_go(SCR_SETTINGS);
    return;
  }
  switch (s_cph) {
    case CP_WAIT_UP: if (!r.down) s_cph = CP_WAIT_DOWN; break;
    case CP_WAIT_DOWN:
      if (r.down) { s_cph = CP_SAMPLING; s_cax = s_cay = s_cn = 0; s_cskip = 3; }
      break;
    case CP_SAMPLING:
      if (!r.down) { s_cph = CP_WAIT_DOWN; break; }
      if (!r.fresh) break;
      if (s_cskip) { s_cskip--; break; }
      s_cax += r.rx;
      s_cay += r.ry;
      if (++s_cn >= 16) {
        s_craw[s_ci][0] = (int32_t)((s_cax + 8) / 16);
        s_craw[s_ci][1] = (int32_t)((s_cay + 8) / 16);
        s_ci++;
        s_cph = CP_WAIT_UP;
        if (s_ci >= 4) cal_finish(now);
      }
      break;
    default: break;
  }
}
static void cal_input(const UiIn &in, uint32_t) {
  if (in.type == IN_HOLD || in.type == IN_CLICK) { ui_toast("Calibration cancelled", 1500); ui_go(SCR_SETTINGS); }
  ui_consume();
}
static void cal_draw(void) {
  gfx_fill(C_BG);
  char t[40];
  snprintf(t, sizeof(t), "Touch calibration  %u / 4", (unsigned)(s_ci < 4 ? s_ci + 1 : 4));
  gfx_text_m(&AF_ROW_FOCUS, 160, 104, t, C_TXT, GA_C);
  gfx_text_m(&AF_BODY_S, 160, 126, s_cph == CP_SAMPLING ? "Hold still " AF_ELL : "Tap and hold the target with a stylus or nail", C_MUT, GA_C);
  gfx_micro(160, 142, s_ci == 3 ? "LAST ONE CHECKS THE RESULT" : "PRESS OR HOLD THE KNOB TO CANCEL", C_DIM, GA_C, true);
  if (s_ci < 4) {
    const int x = k_cal_pts[s_ci][0], y = k_cal_pts[s_ci][1];
    const uint16_t c = s_cph == CP_SAMPLING ? C_UP : C_WARN;
    const float pulse = 10 + 3 * sinf(g_ui.now * 0.008f);
    gfx_ring((float)x, (float)y, pulse, 2, c);
    gfx_line(x - 14.0f, (float)y, x + 14.0f, (float)y, 1.2f, c);
    gfx_line((float)x, y - 14.0f, (float)x, y + 14.0f, 1.2f, c);
    if (s_cph == CP_SAMPLING) gfx_arc((float)x, (float)y, 18, 3, -90, -90 + 360.0f * s_cn / 16, C_UP);
  }
  for (int i = 0; i < 4; i++) gfx_circle(136.0f + i * 16, 160, 3, i < s_ci ? C_UP : C_DOT_IDLE);
}
const Screen SCR_CAL_DEF = {"CAL", cal_enter, cal_leave, cal_input, nullptr, cal_draw, false, true, nullptr, nullptr};

// ============================================================== tap grid ==
#define GRID_TOTAL 300
#define GRID_HIT_PX 6
static uint16_t s_gn = 0, s_gh = 0;
static int16_t s_gerr = -1, s_glx = -1, s_gly = -1;
static void grid_target(uint16_t n, int *x, int *y) {
  const int i = n % 25;
  *x = 32 + (i % 5) * 64;
  *y = 24 + (i / 5) * 48;
}
static void tg_enter(uint32_t) {
  s_gn = s_gh = 0;
  s_gerr = -1;
  s_glx = s_gly = -1;
  hal_log("[tgrid] start: tap each target, %u taps, hit <= %d px", (unsigned)GRID_TOTAL, GRID_HIT_PX);
}
static void tg_input(const UiIn &in, uint32_t) {
  ui_consume();
  if (in.type == IN_HOLD) { ui_go(SCR_SETTINGS); return; }
  if (s_gn >= GRID_TOTAL) {
    if (in.type == IN_TAP || in.type == IN_CLICK) ui_go(SCR_SETTINGS);
    return;
  }
  if (in.type != IN_TAP && in.type != IN_LONG && in.type != IN_SWL && in.type != IN_SWR && in.type != IN_SWU && in.type != IN_SWD) return;
  int tx, ty;
  grid_target(s_gn, &tx, &ty);
  const int dx = in.x - tx, dy = in.y - ty;
  const int err = (int)sqrtf((float)(dx * dx + dy * dy));
  const bool hit = in.type == IN_TAP && err <= GRID_HIT_PX;
  s_gn++;
  if (hit) s_gh++;
  s_gerr = (int16_t)(err > 999 ? 999 : err);
  s_glx = in.x;
  s_gly = in.y;
  hal_log("[tgrid] n=%u target=%d,%d got=%d,%d err=%dpx hit=%d hits=%u", (unsigned)s_gn, tx, ty, in.x, in.y, err, hit ? 1 : 0, (unsigned)s_gh);
  if (s_gn >= GRID_TOTAL)
    hal_log("[tgrid] DONE hits=%u/%u -> %s (need >= %u)", (unsigned)s_gh, (unsigned)GRID_TOTAL, s_gh >= GRID_TOTAL - 1 ? "PASS" : "FAIL", (unsigned)(GRID_TOTAL - 1));
}
static void tg_draw(void) {
  gfx_fill(C_BG);
  char t[48];
  if (s_gn < GRID_TOTAL) {
    snprintf(t, sizeof(t), "TAP GRID %u/%u " AF_DOT " HITS %u " AF_DOT " LAST %dPX", (unsigned)s_gn, (unsigned)GRID_TOTAL, (unsigned)s_gh, (int)s_gerr);
    gfx_micro(160, 3, t, C_DIM, GA_C, true);
    for (int i = 0; i < 25; i++) {
      int x, y;
      grid_target((uint16_t)i, &x, &y);
      gfx_rect(x - 1, y - 1, 3, 3, C_DIM);
    }
    int tx, ty;
    grid_target(s_gn, &tx, &ty);
    gfx_ring((float)tx, (float)ty, GRID_HIT_PX + 1.0f, 1.5f, C_ACC);
    gfx_line(tx - 10.0f, (float)ty, tx + 10.0f, (float)ty, 1, C_ACC);
    gfx_line((float)tx, ty - 10.0f, (float)tx, ty + 10.0f, 1, C_ACC);
  } else {
    const bool pass = s_gh >= GRID_TOTAL - 1;
    snprintf(t, sizeof(t), "%u / %u  %s", (unsigned)s_gh, (unsigned)GRID_TOTAL, pass ? "PASS" : "FAIL");
    gfx_text_m(&AF_TITLE, 160, 110, t, pass ? C_UP : C_DN, GA_C);
    gfx_micro(160, 132, "TAP OR PRESS TO EXIT", C_DIM, GA_C);
  }
  if (s_glx >= 0) {
    gfx_line(s_glx - 4.0f, s_gly - 4.0f, s_glx + 4.0f, s_gly + 4.0f, 1, C_WARN);
    gfx_line(s_glx - 4.0f, s_gly + 4.0f, s_glx + 4.0f, s_gly - 4.0f, 1, C_WARN);
  }
}
void tgrid_debug_set(uint16_t n, uint16_t h) { s_gn = n; s_gh = h; s_gerr = 2; s_glx = 98; s_gly = 74; }
const Screen SCR_TGRID_DEF = {"TGRID", tg_enter, nullptr, tg_input, nullptr, tg_draw, false, false, nullptr, nullptr};

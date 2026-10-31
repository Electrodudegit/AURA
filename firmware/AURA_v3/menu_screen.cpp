// menu_screen.cpp - launcher: GRID (default, 3x2 tiles, 3 pages) and LIST
// (Settings > Menu style). Numbers from face_specs.h (MENU GRID / MENU LIST).
#include "prof.h"
#include "apps.h"
#include "src/assets/face_specs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
using namespace aura::spec;

#define PER_PAGE 6
// P016: Football removed from AppId; every app is on the grid and list.

static uint8_t s_focus = 0, s_prev = 0;
static uint32_t s_move_t0 = 0, s_page_t0 = 0, s_enter_t0 = 0;
static int8_t s_page_dir = 0;
static uint8_t s_prev_page = 0;
static bool s_show_last = false;
// list scroll (content px), eased between s_scroll0 -> s_scroll1
static float s_scroll0 = 0, s_scroll1 = 0;

// ------------------------------------------------------------ list model --
struct LRow { int8_t app; const char *hdr; };  // app >= 0 = row, else header
static const LRow k_rows[] = {
  {-1, "TIME & WEATHER"}, {APP_HOME, nullptr}, {APP_WEATHER, nullptr}, {APP_FORECAST, nullptr}, {APP_SKY, nullptr},
  {APP_AIR, nullptr}, {-1, "MARKETS"}, {APP_CRYPTO, nullptr}, {APP_STOCKS, nullptr}, {-1, "NEWS & SPORTS"},
  {APP_NEWS, nullptr}, {APP_CRICKET, nullptr}, {-1, "TOOLS & BUDDY"}, {APP_FOCUS, nullptr},
  {APP_WORLD, nullptr}, {APP_SYSTEM, nullptr}, {APP_ORACLE, nullptr}, {APP_BUDDY, nullptr}, {-1, "DEVICE"},
  {APP_MACRO, nullptr}, {APP_PC, nullptr}, {APP_SETTINGS, nullptr},
};
#define N_LROWS ((int)(sizeof(k_rows) / sizeof(k_rows[0])))
static int s_row_cy[APP_COUNT];  // content-space centre of each app row
static int s_content_h = 0;
static void list_layout(void) {
  int y = LIST_PADT;
  for (int i = 0; i < N_LROWS; i++) {
    if (k_rows[i].app < 0) y += LIST_HDR;
    else { s_row_cy[k_rows[i].app] = y + LIST_ROW / 2; y += LIST_ROW; }
  }
  s_content_h = y + 8;  // bottom padding below the last row
}
static float max_scroll(void) { return (float)(s_content_h - (LIST_VP1 - LIST_VP0)); }
static float list_scroll_target(uint8_t f, float cur) {
  const int vh = LIST_VP1 - LIST_VP0;
  const int margin = LIST_ROW + LIST_GLASS_H / 2 - 6;
  const int cy = s_row_cy[f];
  float sc = cur;
  if (cy - sc < margin) sc = (float)(cy - margin);
  if (cy - sc > vh - margin) sc = (float)(cy - (vh - margin));
  return ui_clampf(sc, 0, max_scroll());
}
static float list_scroll_now(void) {
  const float e = ease_out((g_ui.now - s_move_t0) / (float)FOCUS_SLIDE_MS);
  return s_scroll0 + (s_scroll1 - s_scroll0) * e;
}
static bool menu_shown(uint8_t) { return true; }
static int vis_n(void) {
  int n = 0;
  for (uint8_t i = 0; i < APP_COUNT; i++) if (menu_shown(i)) n++;
  return n;
}
static int vis_pages(void) { return (vis_n() + PER_PAGE - 1) / PER_PAGE; }
static int vis_of(uint8_t app) {
  int v = 0;
  if (app >= APP_COUNT) app = APP_COUNT - 1;
  for (uint8_t i = 0; i < app; i++) if (menu_shown(i)) v++;
  return v;
}
static uint8_t app_at(int vis) {
  if (vis < 0) vis = 0;
  for (uint8_t i = 0; i < APP_COUNT; i++) {
    if (!menu_shown(i)) continue;
    if (vis-- == 0) return i;
  }
  return APP_SETTINGS;
}
static uint8_t vis_step(int delta) {
  const int n = vis_n();
  int v = vis_of(s_focus) + delta;
  if (v < 0) v = 0;
  if (v >= n) v = n - 1;
  return app_at(v);
}

// ------------------------------------------------------------- lifecycle --
static void menu_enter(uint32_t now) {
  list_layout();
  s_enter_t0 = now;
  s_focus = g_ui.prefs.last_app < APP_COUNT ? g_ui.prefs.last_app : 0;
  if (!menu_shown(s_focus)) s_focus = APP_CRICKET;
  s_show_last = s_focus != APP_HOME;
  s_prev = s_focus;
  s_move_t0 = now - 1000;
  s_page_dir = 0;
  // centre the focused row
  const float c = ui_clampf(s_row_cy[s_focus] - (LIST_VP1 - LIST_VP0) / 2.0f, 0, max_scroll());
  s_scroll0 = s_scroll1 = c;
}
static void set_focus(int f, uint32_t now) {
  if (f < 0) f = 0;
  if (f >= APP_COUNT) f = APP_COUNT - 1;
  if (!menu_shown((uint8_t)f)) f = APP_CRICKET;
  if (f == s_focus) return;
  const float cur = list_scroll_now();
  s_prev = s_focus;
  const uint8_t op = (uint8_t)(vis_of(s_focus) / PER_PAGE);
  s_focus = (uint8_t)f;
  s_move_t0 = now;
  s_scroll0 = cur;
  s_scroll1 = list_scroll_target(s_focus, cur);
  const uint8_t np = (uint8_t)(vis_of(s_focus) / PER_PAGE);
  if (np != op) {
    s_prev_page = op;
    s_page_dir = (int8_t)(np > op ? 1 : -1);
    s_page_t0 = now;
  }
}
static void open_focus(void) {
  if (s_focus != APP_SETTINGS) {
    g_ui.prefs.last_app = s_focus;
    ui_prefs_changed();
  }
  ui_open_app(s_focus);
}
static int grid_hit(int x, int y) {
  const int page = vis_of(s_focus) / PER_PAGE;
  const int n = vis_n();
  for (int i = 0; i < PER_PAGE; i++) {
    const int tx = GRID_X[i % 3], ty = GRID_Y[i / 3];
    if (x >= tx && x < tx + GRID_TILE_W && y >= ty && y < ty + GRID_TILE_H) {
      const int vis = page * PER_PAGE + i;
      return vis < n ? app_at(vis) : -1;
    }
  }
  return -1;
}
static int list_hit(int y) {
  const float sc = list_scroll_now();
  for (int i = 0; i < N_LROWS; i++) {
    if (k_rows[i].app < 0) continue;
    const int a = k_rows[i].app;
    const float cy = LIST_VP0 + s_row_cy[a] - sc;
    if (y >= cy - LIST_ROW / 2 && y < cy + LIST_ROW / 2) return a;
  }
  return -1;
}
static void menu_input(const UiIn &in, uint32_t now) {
  const bool list = g_ui.prefs.menu_list;
  switch (in.type) {
    case IN_ROT: set_focus(vis_step(in.d), now); break;
    case IN_CLICK: open_focus(); break;
    case IN_TAP: {
      if (ui_is_back(in)) break;  // 44 px corner, handled globally
      const int h = list ? list_hit(in.y) : grid_hit(in.x, in.y);
      if (h >= 0) { s_focus = (uint8_t)h; open_focus(); ui_consume(); }
      break;
    }
    case IN_SWL:
      if (!list) {
        const int n = vis_n();
        int v = (vis_of(s_focus) / PER_PAGE + 1) * PER_PAGE;
        if (v >= n) v = n - 1;
        set_focus(app_at(v), now);
        ui_consume();
      }
      break;
    case IN_SWR:
      if (ui_is_back(in)) break;
      if (!list) {
        int p = (int)(vis_of(s_focus) / PER_PAGE) - 1;
        if (p < 0) p = 0;
        set_focus(app_at(p * PER_PAGE), now);
        ui_consume();
      }
      break;
    case IN_SWU: set_focus(vis_step(list ? 4 : 3), now); ui_consume(); break;
    case IN_SWD: set_focus(vis_step(list ? -4 : -3), now); ui_consume(); break;
    default: break;
  }
}

// ------------------------------------------------------------------ grid --
static void last_chip(int x, int y) {
  gfx_rrect(x, y, 29, 12, 5, C_LAST_FILL);
  gfx_rrect_frame(x, y, 29, 12, 5, C_LAST_OUTLINE);
  gfx_micro(x + 15, y + 2, "LAST", C_LAST, GA_C, true);
}
static void grid_tile(int idx, int slot, bool focus, float plate_on) {
  PROF_SCOPE(PL_TILES);
  const int tx = GRID_X[slot % 3], ty0 = GRID_Y[slot / 3];
  if (!gfx_vis(ty0 - 4, GRID_TILE_H + 8)) return;
  const AppDef *a = app_def((uint8_t)idx);
  const int ty = focus && plate_on > 0.5f ? ty0 - 1 : ty0;
  if (!(focus && plate_on > 0.5f)) {
    gfx_tile(tx, ty, GRID_TILE_W, GRID_TILE_H, GRID_R, C_TILE_T, C_TILE, C_TILE_RIM);
  }
  uint32_t step = 0;
  float sc = 1.0f;
  if (focus) {
    const uint32_t since = g_ui.now - s_move_t0;
    if (since < 5 * 40) sc = HOP_SCALE[since / 40];
    step = (g_ui.now / ICON_STEP_MS) + 1;
  }
  const int dy = focus ? -1 - (int)(2 * (sc - 1) / 0.08f) : 0;
  w_icon(a->icon, 48, tx + 24, ty0 + 9 + dy, focus ? C_TXT : C_ICON, step, sc);
  gfx_text_m(&AF_LABEL, tx + 48, ty0 + 64 + (focus ? -1 : 0), a->name, focus ? C_TXT : C_MUT, GA_C);
  char v[24];
  uint16_t vc;
  app_value((uint8_t)idx, v, sizeof(v), &vc);
  if (v[0]) gfx_micro(tx + 48, ty0 + 72 + (focus ? -1 : 0), v, vc, GA_C, true);
  if (focus && s_show_last && idx == g_ui.prefs.last_app && idx != APP_HOME) last_chip(tx + 60, ty + 5);
  if (focus && plate_on > 0.5f) gfx_rrect(tx + 36, ty0 + 81, 25, 3, 1, C_ACC);
}
static int page_app(int page, int slot) {
  const int vis = page * PER_PAGE + slot;
  return vis < vis_n() ? app_at(vis) : -1;
}
static void grid_page(int page, bool live) {
  // moving focus plate (lerp between tiles on the same page)
  const uint32_t since = g_ui.now - s_move_t0;
  const int fv = vis_of(s_focus);
  const int fs = fv % PER_PAGE;
  const bool same_page = (fv / PER_PAGE) == page;
  if (live && same_page) {
    const int pv = vis_of(s_prev);
    const bool prev_same = (pv / PER_PAGE) == page;
    float e = ease_out(since / (float)FOCUS_SLIDE_MS);
    if (!prev_same) e = 1;
    const int ps = prev_same ? pv % PER_PAGE : fs;
    const float px = GRID_X[ps % 3] + (GRID_X[fs % 3] - GRID_X[ps % 3]) * e;
    const float py = GRID_Y[ps / 3] + (GRID_Y[fs / 3] - GRID_Y[ps / 3]) * e;
    for (int i = 0; i < PER_PAGE; i++) {
      const int idx = page_app(page, i);
      if (idx < 0) break;
      if (i != fs || e < 1) grid_tile(idx, i, false, 0);
    }
    gfx_rrect((int)px + 1, (int)py + 3 - 1, GRID_TILE_W, GRID_TILE_H, GRID_R, C_SHADOW);
    gfx_tile((int)px, (int)py - 1, GRID_TILE_W, GRID_TILE_H, GRID_R, C_TILE_HI_TOP, C_TILE_HI, blend565(C_TILE_HI_TOP, C_TXT, 24));
    gfx_rrect_frame((int)px, (int)py - 1, GRID_TILE_W, GRID_TILE_H, GRID_R, C_FOCUS_BORDER);
    for (int i = 0; i < PER_PAGE; i++) {
      const int idx = page_app(page, i);
      if (idx < 0) break;
      if (i == fs) grid_tile(idx, i, true, e);
      else if (e < 1 && fabsf(px - GRID_X[i % 3]) < GRID_TILE_W && fabsf(py - GRID_Y[i / 3]) < GRID_TILE_H) {
        // content of a tile the plate is sliding over stays readable
        const AppDef *a = app_def((uint8_t)idx);
        w_icon(a->icon, 48, GRID_X[i % 3] + 24, GRID_Y[i / 3] + 9, C_ICON);
        gfx_text_m(&AF_LABEL, GRID_X[i % 3] + 48, GRID_Y[i / 3] + 64, a->name, C_MUT, GA_C);
      }
    }
  } else {
    for (int i = 0; i < PER_PAGE; i++) {
      const int idx = page_app(page, i);
      if (idx < 0) break;
      grid_tile(idx, i, false, 0);
    }
  }
  const int rem = vis_n() % PER_PAGE;
  if (page == vis_pages() - 1 && rem && gfx_vis(150, 40)) {  // empty slot hint
    const int i = rem;
    const int cx = GRID_X[i % 3] + GRID_TILE_W / 2, cy = GRID_Y[i / 3] + GRID_TILE_H / 2;
    const uint16_t hc = blend565(C_BG, C_DIM, 200);
    gfx_micro(cx, cy - 16, "PRESS OPEN", hc, GA_C, true);
    gfx_micro(cx, cy - 4, "HOLD BACK", hc, GA_C, true);
    gfx_micro(cx, cy + 8, "SWIPE PAGE", hc, GA_C, true);
  }
}
static void grid_draw(void) {
  const int page = vis_of(s_focus) / PER_PAGE;
  const uint32_t since = g_ui.now - s_page_t0;
  if (s_page_dir && since < PAGE_SLIDE_MS) {
    const float e = ease_out(since / (float)PAGE_SLIDE_MS);
    const int off = (int)(320 * e);
    gfx_origin(-off * s_page_dir, 0);
    grid_page(s_prev_page, false);
    gfx_origin(320 * s_page_dir - off * s_page_dir, 0);
    grid_page(page, true);
    gfx_origin(0, 0);
  } else {
    grid_page(page, true);
  }
  w_page_dots(page, vis_pages(), GRID_DOTS_Y, C_ACC, C_DOT_IDLE);
}

// ------------------------------------------------------------------ list --
static void list_draw(void) {
  const float sc = list_scroll_now();
  const float e = ease_out((g_ui.now - s_move_t0) / (float)FOCUS_SLIDE_MS);
  gfx_clip(0, LIST_VP0, GFX_W, LIST_VP1);
  // glass row (slides old -> new)
  const float gcy = LIST_VP0 + (s_row_cy[s_prev] + (s_row_cy[s_focus] - s_row_cy[s_prev]) * e) - sc;
  {
    const int gy = (int)(gcy - LIST_GLASS_H / 2.0f), gw = LIST_GLASS_X1 - LIST_GLASS_X0;
    if (gfx_vis(gy - 3, LIST_GLASS_H + 6)) {
      static const uint8_t ga[3] = {41, 77, 128};
      for (int g = 3; g >= 1; g--)
        gfx_rrect_frame(LIST_GLASS_X0 - g, gy - g, gw + 2 * g, LIST_GLASS_H + 2 * g, LIST_GLASS_R + g, C_ACC, ga[3 - g] / 3);
      gfx_tile(LIST_GLASS_X0, gy, gw, LIST_GLASS_H, LIST_GLASS_R, C_TILE_HI_TOP, C_TILE_HI, blend565(C_TILE_HI_TOP, C_TXT, 30));
      gfx_rrect_frame(LIST_GLASS_X0, gy, gw, LIST_GLASS_H, LIST_GLASS_R, C_FOCUS_BORDER);
      gfx_rrect(11, (int)gcy - 11, 3, 22, 1, C_ACC);
    }
  }
  for (int i = 0; i < N_LROWS; i++) {
    const LRow &r = k_rows[i];
    if (r.app < 0) {
      // header: slot top = row top of the next row - LIST_HDR
      int ytop;
      if (i + 1 < N_LROWS && k_rows[i + 1].app >= 0) ytop = s_row_cy[k_rows[i + 1].app] - LIST_ROW / 2 - LIST_HDR;
      else continue;
      const int y = LIST_VP0 + ytop - (int)sc;
      if (!gfx_vis(y, LIST_HDR)) continue;
      const uint16_t hc = blend565(C_BG, C_DIM, 200);
      const int w = gfx_micro_w(r.hdr, true);
      gfx_micro(22, y + 5, r.hdr, hc, GA_L, true);
      gfx_hline(22 + w + 8, y + 10, 300 - (22 + w + 8), blend565(C_BG, C_LINE, 110));
      continue;
    }
    const uint8_t a = (uint8_t)r.app;
    const int cy = LIST_VP0 + s_row_cy[a] - (int)sc;
    if (!gfx_vis(cy - LIST_ROW / 2, LIST_ROW)) continue;
    const AppDef *d = app_def(a);
    const bool f = a == s_focus;
    char v[24];
    uint16_t vc;
    app_value(a, v, sizeof(v), &vc);
    if (f) {
      gfx_circle(LIST_ICON_CX, (float)cy, 16, blend565(C_TILE_HI, C_ACC, 50));
      w_icon(d->icon, 24, LIST_ICON_CX - 12, cy - 12, C_TXT, g_ui.now / ICON_STEP_MS + 1);
      const int nx = gfx_text_m(&AF_ROW_FOCUS, LIST_NAME_X, cy - 7, d->name, C_TXT);
      const int nw = gfx_text_w(&AF_ROW_FOCUS, d->name);
      if (s_show_last && a == g_ui.prefs.last_app && a != APP_HOME) {
        const int lx = nx + nw + 8;
        gfx_rrect(lx, cy - 12, 29, 11, 5, C_LAST_FILL);  // Q02: 29x11 r5 spec chip
        gfx_rrect_frame(lx, cy - 12, 29, 11, 5, C_LAST_OUTLINE);
        gfx_micro(lx + 15, cy - 10, "LAST", C_LAST, GA_C, true);
      }
      gfx_text_m(&AF_BODY_S, LIST_NAME_X, cy + 9, d->sub, C_MUT);
      if (v[0]) gfx_text_m(&AF_LABEL_M, LIST_VALUE_RX, cy - 1, v, vc == C_MUT ? C_TXT : vc, GA_R);
    } else {
      w_icon(d->icon, 24, LIST_ICON_CX - 12, cy - 12, blend565(C_BG, C_ICON, 170));
      gfx_text_m(&AF_ROW, LIST_NAME_X, cy, d->name, blend565(C_BG, C_MUT, 235));
      if (v[0]) gfx_micro(LIST_VALUE_RX, cy - 5, v, blend565(C_BG, vc, 190), GA_R);
    }
  }
  // fades under the status bar and at the bottom
  for (int k = 0; k < LIST_FADE_TOP; k++) gfx_rect_a(0, LIST_VP0 + k, 310, 1, C_BG, (uint8_t)(255 - k * 255 / LIST_FADE_TOP));
  for (int k = 0; k < LIST_FADE_BOTTOM; k++) gfx_rect_a(0, LIST_VP1 - 1 - k, 310, 1, C_BG, (uint8_t)(255 - k * 255 / LIST_FADE_BOTTOM));
  gfx_noclip();
  // rail
  if (gfx_vis(LIST_RAIL_Y0, LIST_RAIL_Y1 - LIST_RAIL_Y0)) {
    const int th = LIST_RAIL_Y1 - LIST_RAIL_Y0;
    gfx_rrect(LIST_RAIL_X, LIST_RAIL_Y0, 3, th, 1, blend565(C_BG, C_LINE, 150));
    int tl = th * (LIST_VP1 - LIST_VP0) / s_content_h;
    if (tl < LIST_THUMB_MIN) tl = LIST_THUMB_MIN;
    const float ms = max_scroll();
    const int ty = LIST_RAIL_Y0 + (int)((th - tl) * (ms > 0 ? sc / ms : 0));
    gfx_rrect(LIST_RAIL_X - 1, ty, 5, tl, 2, C_ACC);
  }
}

static void menu_draw(void) {
  gfx_fill(C_BG);
  if (g_ui.prefs.menu_list) list_draw();
  else grid_draw();
  char pos[24];
  const unsigned v = (unsigned)(vis_of(s_focus) + 1), n = (unsigned)vis_n();
  if (g_ui.prefs.menu_list) snprintf(pos, sizeof(pos), "%02u/%02u", v, n);
  else snprintf(pos, sizeof(pos), "P%u/%u " AF_DOT " %02u/%02u", (unsigned)(vis_of(s_focus) / PER_PAGE + 1), (unsigned)vis_pages(), v, n);
  w_status("APPS", "");
  // Q05: >= 8 px gap to the status icons: end at x 225 when the BT dot is shown (dot ~x233), else 235
  if (gfx_vis(0, 24)) gfx_micro(g_ui.d.ble.connected ? 225 : 235, 8, pos, C_MUT, GA_R, true);
}

const Screen SCR_MENU_DEF = {"MENU", menu_enter, nullptr, menu_input, nullptr, menu_draw, true, false, nullptr, nullptr};
// host capture helper
void menu_debug_focus(uint8_t f, bool last) { s_focus = s_prev = f; s_show_last = last; s_move_t0 = g_ui.now - 1000; s_page_dir = 0;
  const float c = ui_clampf(s_row_cy[f] - (LIST_VP1 - LIST_VP0) / 2.0f, 0, max_scroll()); s_scroll0 = s_scroll1 = c; }

// home_screen.cpp - home faces F1 split-flap (default), F2 buddy clock, F5 weather
// scene, F6 analog. Matte C_BG backgrounds (v3.1.2: the aurora is boot-only). Numbers from src/assets/face_specs.h.
#include "prof.h"
#include "apps.h"
#include "src/assets/face_specs.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
using namespace aura::spec;

static const char *const k_wd[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *const k_mo[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
                                     "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
static const uint16_t k_aur[3] = {AUR0, AUR1, AUR2};

// ------------------------------------------------------------ face state --
static uint8_t s_prev_face = 0;
static uint32_t s_slide_t0 = 0;
static int8_t s_slide_dir = 0;
static uint32_t s_enter_ms = 0;
static char s_digits[5] = "----", s_old[5] = "----";
static uint32_t s_flip_t0[4] = {0, 0, 0, 0};
#define SLIDE_MS 220



// ----------------------------------------------------------- flap tiles --
static void flap_digit(int x, int y, char ch, int clip_y0, int clip_y1) {
  char s[2] = {ch, 0};
  gfx_clip(x, clip_y0, x + FLAP_W, clip_y1);
  if (ch >= '0' && ch <= '9') {
    const int cap = gfx_aacap_h(FAA_B74D), w = gfx_aatext_w(FAA_B74D, s);
    const int bx = x + FLAP_W / 2 - w / 2, by = y + 46 + cap / 2;
    gfx_aatext(FAA_B74D, bx, by + 3, s, C_HOME_FLAP_HINGE);
    gfx_aatext(FAA_B74D, bx, by, s, H_TXT);
  } else {  // '-' (no time yet): the digits font has no dash
    gfx_rrect(x + 20, y + 42, 30, 8, 3, H_DIM);
  }
  gfx_noclip();
}
static void flap_body(int x, int y, int ya, int yb) {
  // tile face (alpha over the aurora), clipped to rows [ya, yb)
  gfx_clip(x - 4, ya, x + FLAP_W + 4, yb);
  gfx_rrect(x, y, FLAP_W, FLAP_H, FLAP_R, C_HOME_FLAP_BOT, FLAP_ALPHA);
  gfx_clip(x, ya > y ? ya : y, x + FLAP_W, yb < y + FLAP_H / 2 - 2 ? yb : y + FLAP_H / 2 - 2);
  gfx_rrect(x, y, FLAP_W, FLAP_H, FLAP_R, C_HOME_FLAP_TOP, FLAP_ALPHA);
  for (int r = 4; r < FLAP_H / 2 - 2; r++)
    gfx_rect_a(x + 3, y + r, FLAP_W - 6, 1, C_HOME_FLAP_GLOSS, (uint8_t)((46 - r) * 90 / 46));
  gfx_noclip();
}
// shadow + baked accent glow + tile body: everything under the digit
static void tile_base(int x, int y, bool glow) {
  static const uint8_t sa[3] = {40, 55, 90};
  for (int g = 0; g < 3; g++) gfx_rrect(x + 1 - (3 - g), y + 4 - (3 - g), FLAP_W + 2 * (3 - g), FLAP_H + 2 * (3 - g) - 2 * g, 12 + (3 - g), 0x0000, sa[g]);
  if (glow) {
    static const uint8_t ga[3] = {31, 56, 97};
    for (int g = 3; g >= 1; g--) gfx_rrect_frame(x - g, y - g, FLAP_W + 2 * g, FLAP_H + 2 * g, FLAP_R + g, A_HOME, ga[3 - g]);
  }
  flap_body(x, y, y, y + FLAP_H);
}

// v3.1.1 tile cache: the base (shadow, glow, translucent body, gloss) is rendered ONCE into a
// premultiplied sprite out = P + bg * K (P RGB565, K 8-bit transmission) and shared by the 4
// tiles; the aurora still shows through exactly as before (K ~ 7% inside the body). Only the
// digit, hinge and pins are drawn per frame; a flip animates on top of the cached base.
#define TC_X0 4
#define TC_Y0 4
#define TC_W (FLAP_W + 9)
#define TC_H (FLAP_H + 12)
#define TC_BYTES (TC_W * TC_H * 3)
static uint16_t *s_tc_p = nullptr;
static uint8_t *s_tc_k = nullptr;
static bool s_tc_failed = false;
static void tile_cache_free(void) {
  free(s_tc_p); free(s_tc_k);
  s_tc_p = nullptr; s_tc_k = nullptr;
}
#ifndef HOME_TILE_SPRITE
#define HOME_TILE_SPRITE 0  // v3.1.3: F1 24.6 KB tile sprite off (band keys make it unnecessary)
#endif
#if HOME_TILE_SPRITE
static bool tile_cache_build(void) {
  if (s_tc_p) return true;
  if (s_tc_failed) return false;
  if (!hal_cache_ok(TC_BYTES)) { s_tc_failed = true; return false; }
  s_tc_p = (uint16_t *)malloc(TC_W * TC_H * 2);
  s_tc_k = (uint8_t *)malloc(TC_W * TC_H);
  uint16_t *tmp = (uint16_t *)malloc(TC_W * 8 * 2);
  if (!s_tc_p || !s_tc_k || !tmp) { free(tmp); tile_cache_free(); s_tc_failed = true; return false; }
  gfx_begin_ex(s_tc_p, 0, TC_H, TC_W);  // pass 1: over black -> P
  gfx_fill(0x0000);
  tile_base(TC_X0, TC_Y0, true);
  for (int y0 = 0; y0 < TC_H; y0 += 8) {  // pass 2: over white, 8-row strips -> K
    const int hh = TC_H - y0 < 8 ? TC_H - y0 : 8;
    gfx_begin_ex(tmp, y0, hh, TC_W);
    gfx_fill(0xFFFF);
    tile_base(TC_X0, TC_Y0, true);
    for (int i = 0; i < TC_W * hh; i++) {
      const uint16_t pw = (uint16_t)((tmp[i] >> 8) | (tmp[i] << 8)), pv = s_tc_p[y0 * TC_W + i];
      const uint16_t pb = (uint16_t)((pv >> 8) | (pv << 8));
      const int kr = (((pw >> 11) & 31) - ((pb >> 11) & 31)) * 256 / 31, kg = (((pw >> 5) & 63) - ((pb >> 5) & 63)) * 256 / 63,
                kb = ((pw & 31) - (pb & 31)) * 256 / 31;
      int k = (kr + 2 * kg + kb + 2) / 4;
      if (k < 0) k = 0;
      if (k > 255) k = 255;
      if (k < 3) k = 0;  // rounding noise of an opaque pixel
      s_tc_k[y0 * TC_W + i] = (uint8_t)k;
    }
  }
  free(tmp);
  gfx_begin_ex(nullptr, 0, 0, GFX_W);
  hal_log("[ui] tile cache %u B (heap %lu largest %lu)", (unsigned)TC_BYTES, (unsigned long)hal_heap_free(), (unsigned long)hal_heap_largest());
  return true;
}
#endif
uint32_t home_cache_bytes(void);

void home_flap(int x, int y, char ch, char prev, float u, bool glow) {
  PROF_SCOPE(PL_TILES);
  if (!gfx_vis(y - 4, FLAP_H + 10)) return;
  const int hinge = y + 46;
  if (glow && s_tc_p) gfx_blit_pk(x - TC_X0, y - TC_Y0, TC_W, TC_H, s_tc_p, s_tc_k);
  else tile_base(x, y, glow);
  const bool flip = u >= 0 && u < 1 && prev != ch;
  if (!flip) {
    flap_digit(x, y, ch, y, y + FLAP_H);
  } else if (u < 0.5f) {
    const float k = gfx_fcos(3.14159f * u);
    const int fh = (int)(46 * k);
    // revealed new top + old bottom (base already drawn)
    flap_digit(x, y, ch, y, hinge);
    flap_digit(x, y, prev, hinge, y + FLAP_H);
    gfx_rect_a(x + 2, y + 2, FLAP_W - 4, 44, 0x0000, (uint8_t)(150 * k * 0.6f));
    // folding old top flap (height 46k anchored at the hinge)
    if (fh > 0) {
      flap_body(x, y, hinge - fh, hinge);
      flap_digit(x, y, prev, hinge - fh, hinge);
      gfx_rect_a(x + 2, hinge - fh, FLAP_W - 4, fh, 0x0000, (uint8_t)(170 * (1 - k)));
    }
  } else {
    const float k = -gfx_fcos(3.14159f * u);
    const int fh = (int)(46 * k);
    flap_digit(x, y, ch, y, hinge);
    flap_digit(x, y, prev, hinge, y + FLAP_H);
    gfx_rect_a(x + 2, hinge, FLAP_W - 4, 44, 0x0000, (uint8_t)(160 * (1 - k)));
    if (fh > 0) {  // new bottom flap drops
      flap_body(x, y, hinge, hinge + fh);
      flap_digit(x, y, ch, hinge, hinge + fh);
      gfx_rect_a(x + 2, hinge, FLAP_W - 4, fh, 0x0000, (uint8_t)(110 * (1 - k)));
      gfx_hline(x + 3, hinge + fh - 1, FLAP_W - 6, blend565(C_HOME_FLAP_BOT, RGB565C(170, 190, 220), (uint8_t)(160 * (1 - k))));
    }
  }
  // rim, gloss, hinge, pins
  gfx_hline(x + 10, y + 1, FLAP_W - 20, blend565(C_HOME_FLAP_TOP, RGB565C(170, 190, 220), 110));
  gfx_rect(x + 2, hinge - 1, FLAP_W - 4, 2, C_HOME_FLAP_HINGE);
  gfx_hline(x + 2, hinge + 1, FLAP_W - 4, C_HOME_FLAP_HINGE_LT);
  for (int s = 0; s < 2; s++) {
    const int px = s ? x + FLAP_W - 3 : x - 3;
    gfx_rrect(px, hinge - 6, 5, 12, 2, C_HOME_PIN);
    gfx_vline(px + 1, hinge - 4, 8, C_HOME_PIN_LT);
  }
}

// ------------------------------------------------------------- helpers --
static void time_str(char *b, int n) {
  if (g_ui.tv) snprintf(b, n, "%02d:%02d", g_ui.lt.tm_hour, g_ui.lt.tm_min);
  else snprintf(b, n, "--:--");
}
static int aqi_band(int aqi) {
  int i = 0;
  while (i < 5 && aqi > AQI_BREAKS[i + 1]) i++;
  return i;
}
static void up_copy(char *dst, int n, const char *src) {
  int i = 0;
  for (; src[i] && i < n - 1; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
  dst[i] = 0;
}
static bool f5_particles(void) {
  const uint8_t kind = g_ui.d.wx.valid ? wx_kind(g_ui.d.wx.code) : 1;
  return kind == 2 || kind == 3 || kind == 4;
}
static HeroMask s_hero[2];  // [0] F2 (B58D), [1] F5 (B74D); rebuilt when the minute changes
static void hero_time(const AAFont *f, int cx, int cy, uint16_t glow, uint8_t ga) {
  char t[8];
  time_str(t, sizeof(t));
  const int cap = gfx_aacap_h(f);
  if (g_ui.tv && !gfx_vis(cy - cap / 2 - 4, cap + 8)) return;
  HeroMask *hm = &s_hero[f == FAA_B74D ? 1 : 0];
  // v3.1.3: band keys keep static hero bands off the compose path, so a mask only pays off
  // where the hero is recomposed every frame (F5 with rain/snow particles). 12.3 KB, built
  // only via hal_cache_ok() (TLS reserve + release-before-TLS), dropped otherwise.
  const bool want = f == FAA_B74D && f5_particles();
  if (!want && hm->m) gfx_hero_free(hm);
  if (g_ui.tv && want && gfx_hero_build(hm, f, cx, cy, t, ga)) gfx_hero_draw(hm, H_TXT, C_HOME_DIGIT_SHADOW, glow);
  else if (g_ui.tv) gfx_aatext_hero(f, cx, cy, t, H_TXT, C_HOME_DIGIT_SHADOW, glow, ga);
  else {
    const int ch = gfx_aacap_h(f), w = gfx_aatext_w(f, "00:00");
    for (int i = 0; i < 4; i++) {
      const int xx = cx - w / 2 + i * w / 4 + (i >= 2 ? 10 : 0) + 6;
      gfx_rrect(xx, cy - ch / 10, w / 4 - 22, ch / 6, 3, H_DIM);
    }
  }
}

// ------------------------------------------------------------------ F1 --
static void gauge(int i, float frac, const char *val, const char *lbl, uint16_t col, bool have) {
  PROF_SCOPE(PL_FACE);
  const int cx = GAUGE_CX[i], cy = GAUGE_CY, r = GAUGE_R;
  if (!gfx_vis(cy - r - 10, 2 * r + 30)) return;
  for (int k = 0; k < GAUGE_TICKS; k++) {
    const float a = (GAUGE_A0 + k * GAUGE_TICK_STEP) * 0.0174533f;
    const bool maj = k % 5 == 0;
    const float r1 = r + 5.0f, r2 = r + (maj ? 8.0f : 7.0f);
    gfx_line(cx + gfx_fcos(a) * r1, cy + gfx_fsin(a) * r1, cx + gfx_fcos(a) * r2, cy + gfx_fsin(a) * r2, 1, maj ? H_MUT : blend565(C_BG, H_DIM, 150));
  }
  const uint32_t el = g_ui.now - s_enter_ms;
  float e = ui_clampf(el / (float)GAUGE_COUNTUP_MS, 0, 1);
  e = 1 - (1 - e) * (1 - e) * (1 - e) * (1 - e);
  const float f = have ? ui_clampf(frac, 0, 1) * e : 0;
  // track + fill in one annulus pass (v3.1.1)
  gfx_arc2((float)cx, (float)cy, (float)r + 2, GAUGE_THICK, GAUGE_A0, GAUGE_SWEEP, H_SURF2, f > 0.005f ? GAUGE_SWEEP * f : -1, col);
  if (f > 0.005f) {
    const float a1 = GAUGE_A0 + GAUGE_SWEEP * f;
    const float ar = a1 * 0.0174533f;
    gfx_circle(cx + gfx_fcos(ar) * r, cy + gfx_fsin(ar) * r, 3, H_TXT);
    if (el > 1200) {  // comet along the filled sweep
      const float span = GAUGE_SWEEP * f;
      const float head = fmodf((el - 1200) * 0.12f, span > 1 ? span : 1);
      static const float rr[3] = {2, 1.5f, 1};
      static const uint8_t mix[3] = {199, 130, 61};
      for (int d = 0; d < 3; d++) {
        float ad = head - d * 7;
        if (ad < 0) continue;
        const float aa = (GAUGE_A0 + ad) * 0.0174533f;
        gfx_circle(cx + gfx_fcos(aa) * r, cy + gfx_fsin(aa) * r, rr[d], blend565(col, H_TXT, mix[d]));
      }
    }
  }
  gfx_text_m(&AF_LABEL_M, cx, cy, val, H_TXT, GA_C);
  gfx_micro(cx, 198, lbl, H_DIM, GA_C, true);
}
static void face_f1(void) {
  const float t = g_ui.now / 1000.0f;
  (void)t;  // v3.1.2: matte C_BG background (aurora is boot-only)
  for (int i = 0; i < 4; i++) {
    float u = -1;
    if (s_flip_t0[i]) {
      const int32_t el = (int32_t)(g_ui.now - s_flip_t0[i]);
      if (el < 0) u = 0;
      else if (el < FLIP_MS) u = el / (float)FLIP_MS;
    }
    home_flap(FLAP_X[i], FLAP_Y, s_digits[i], s_old[i], u, true);
  }
  if (gfx_vis(56, 40)) {
    const bool on = !g_ui.tv || (g_ui.now % 1000) < 500;
    const uint16_t cc = on ? A_HOME : blend565(C_BG, A_HOME, 100);
    gfx_circle(COLON_X, 76 - COLON_DY, COLON_R, cc);
    gfx_circle(COLON_X, 76 + COLON_DY, COLON_R, cc);
  }
  // date line
  if (gfx_vis(128, 22)) {
    const char *wd = g_ui.tv ? k_wd[g_ui.lt.tm_wday % 7] : "--";
    char dm[24];
    if (g_ui.tv) snprintf(dm, sizeof(dm), "%d %s", g_ui.lt.tm_mday, k_mo[g_ui.lt.tm_mon % 12]);
    else snprintf(dm, sizeof(dm), "SYNCING TIME");
    const int w1 = gfx_text_w(&AF_LABEL_M, wd, 2), w2 = gfx_text_w(&AF_BODY, dm, 1);
    const int x0 = 160 - (w1 + 18 + w2) / 2;
    gfx_text_m(&AF_LABEL_M, x0, 139, wd, H_TXT, GA_L, 2);
    gfx_circle(x0 + w1 + 9.0f, 139.5f, 1.8f, A_HOME);
    gfx_text_m(&AF_BODY, x0 + w1 + 18, 139, dm, H_MUT, GA_L, 1);
  }
  // seconds bar
  if (gfx_vis(SEC_Y - 3, 7)) {
    gfx_hline(SEC_X0, SEC_Y, SEC_X1 - SEC_X0, blend565(C_BG, H_TXT, 33));
    if (g_ui.tv) {
      const float frac = (g_ui.lt.tm_sec + (g_ui.now % 1000) / 1000.0f) / 60.0f;
      const int px = (int)((SEC_X1 - SEC_X0) * frac);
      gfx_hline(SEC_X0, SEC_Y, px, blend565(C_BG, A_HOME, 209));  // flat accent (was the aurora gradient)
      gfx_circle(SEC_X0 + px + 0.5f, SEC_Y + 0.5f, 2, H_TXT);
    }
  }
  const AuraData &d = g_ui.d;
  char v[3][12];
  if (d.wx.valid) fmt_temp(v[0], 12, d.wx.temp_x10); else snprintf(v[0], 12, "--");
  if (d.wx.valid) snprintf(v[1], 12, "%u%%", (unsigned)d.wx.hum); else snprintf(v[1], 12, "--");
  if (d.air.valid) snprintf(v[2], 12, "%u", (unsigned)d.air.aqi); else snprintf(v[2], 12, "--");
  gauge(0, d.wx.temp_x10 / 450.0f, v[0], "TEMP", A_WEATHER, d.wx.valid);
  gauge(1, d.wx.hum / 100.0f, v[1], "HUMID", A_FORECAST, d.wx.valid);
  gauge(2, d.air.aqi / 200.0f, v[2], "AQI", d.air.valid ? AQI_BAND_HOME_COLORS[aqi_band(d.air.aqi)] : H_WARN, d.air.valid);
  if (gfx_vis(212, 14)) {
    char f[80], cond[24], city[24];
    up_copy(cond, sizeof(cond), d.wx.valid ? wx_text(d.wx.code) : "NO WEATHER YET");
    up_copy(city, sizeof(city), d.loc.city[0] ? d.loc.city : "SET LOCATION");
    if (d.wx.valid && d.wx.ndays)
      snprintf(f, sizeof(f), "%s " AF_DOT " %s " AF_DOT " H%d L%d", cond, city, (d.wx.day[0].hi_x10 + 5) / 10, (d.wx.day[0].lo_x10 + 5) / 10);
    else snprintf(f, sizeof(f), "%s " AF_DOT " %s", cond, city);
    gfx_micro(160, 212, f, H_MUT, GA_C, true);
  }
}

// ------------------------------------------------------------------ F2 --
#define F2_BX 160.0f
#define F2_BY 74.6f
#define F2_BS 0.74f
#define F2_MENU_DELAY_MS 250  // v3.1.4: press / tap pops the face, the menu opens 250 ms later
static uint32_t s_menu_at = 0;    // pending menu deadline (0 none)
static void face_f2(void) {
  // v3.1.2: matte C_BG background (aurora is boot-only). v3.1.4: Buddy v2 rig (ticked in home_tick).
  buddy_render(&g_rig, F2_BX, F2_BY, F2_BS, C_BG);
  hero_time(FAA_B58D, F2_TIME.x, F2_TIME.y, A_HOME, 90);
  if (gfx_vis(F2_INFO_Y - 2, 16)) {
    char b[64];
    const AuraData &d = g_ui.d;
    char tt[10], cond[20];
    if (d.wx.valid) fmt_temp(tt, sizeof(tt), d.wx.temp_x10); else snprintf(tt, sizeof(tt), "--");
    up_copy(cond, sizeof(cond), d.wx.valid ? wx_text(d.wx.code) : "--");
    if (g_ui.tv) {
      char wd[4] = {0}, mo[4] = {0};
      memcpy(wd, k_wd[g_ui.lt.tm_wday % 7], 3);
      memcpy(mo, k_mo[g_ui.lt.tm_mon % 12], 3);
      snprintf(b, sizeof(b), "%s %d %s  " AF_DOT "  %s  " AF_DOT "  %s", wd, g_ui.lt.tm_mday, mo, tt, cond);
    } else snprintf(b, sizeof(b), "%s  " AF_DOT "  %s", tt, cond);
    gfx_micro(160, F2_INFO_Y, b, RGB565C(220, 228, 240), GA_C);
  }
}

// ------------------------------------------------------------------ F5 --
static void face_f5(void) {
  const float t = g_ui.now / 1000.0f * 1.2f;
  const AuraData &d = g_ui.d;
  gfx_vgrad(0, 24, GFX_W, 216, C_HOME_RAIN_TOP, C_HOME_RAIN_BOT);
  const uint8_t kind = d.wx.valid ? wx_kind(d.wx.code) : 1;
  if (gfx_vis(30, 60)) w_wx_glyph(262, 58, d.wx.valid ? d.wx.code : 3, d.wx.valid ? d.wx.is_day : true, 48, d.wx.wind_x10);
  if (kind == 2 || kind == 3 || kind == 4) {
    uint32_t s = 0x5EED1234u;
    for (int i = 0; i < 26; i++) {
      s = s * 1664525u + 1013904223u;
      const int x0 = (int)(s % 320);
      s = s * 1664525u + 1013904223u;
      const float v = (60 + (s % 61)) * 1.0f;
      s = s * 1664525u + 1013904223u;
      const float y0 = (float)(s % 200);
      const float y = 24 + fmodf(y0 + v * t, 208.0f);
      if (!gfx_vis((int)y, 10)) continue;
      if (kind == 4) gfx_circle((float)x0, y, 1.3f, H_TXT);
      else gfx_line((float)x0, y, x0 - 3.0f, y + 9, 1, C_HOME_RAIN);
    }
  }
  hero_time(FAA_B74D, F5_TIME.x, F5_TIME.y, A_FORECAST, 120);
  if (gfx_vis(F5_DATE_Y - 2, 16)) {
    char b[64], tt[10], cond[20];
    if (d.wx.valid) fmt_temp(tt, sizeof(tt), d.wx.temp_x10); else snprintf(tt, sizeof(tt), "--");
    up_copy(cond, sizeof(cond), d.wx.valid ? wx_text(d.wx.code) : "NO WEATHER YET");
    if (g_ui.tv) {
      char wd[4] = {0}, mo[4] = {0};
      memcpy(wd, k_wd[g_ui.lt.tm_wday % 7], 3);
      memcpy(mo, k_mo[g_ui.lt.tm_mon % 12], 3);
      snprintf(b, sizeof(b), "%s %d %s  " AF_DOT "  %s %s", wd, g_ui.lt.tm_mday, mo, tt, cond);
    } else snprintf(b, sizeof(b), "%s %s", tt, cond);
    gfx_micro(160, F5_DATE_Y, b, RGB565C(225, 232, 242), GA_C);
  }
  if (gfx_vis(F5_RULE_Y, 1))
    gfx_hline(0, F5_RULE_Y, 320, blend565(RGB565C(12, 17, 26), A_HOME, 79));  // flat (was the aurora gradient)
  if (!gfx_vis(F5_STRIP.y, F5_STRIP.h)) return;
  gfx_rrect(F5_STRIP.x, F5_STRIP.y, F5_STRIP.w, F5_STRIP.h, 10, C_BG, 153);
  gfx_rrect_frame(F5_STRIP.x, F5_STRIP.y, F5_STRIP.w, F5_STRIP.h, 10, blend565(C_BG, A_FORECAST, 110));
  if (!d.wx.valid || !d.wx.ndays) {
    gfx_micro(160, 186, "FORECAST LOADS WHEN ONLINE", H_MUT, GA_C, true);
    return;
  }
  static const char *const wd3[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  for (int i = 0; i < 4 && i < d.wx.ndays; i++) {
    const int x = F5_DAY_X0 + F5_DAY_STEP * i;
    const char *lbl = i == 0 ? "TODAY" : wd3[(g_ui.lt.tm_wday + i) % 7];
    gfx_micro(x + 28, 166, lbl, i == 0 ? H_TXT : H_MUT, GA_C, true);
    w_wx_glyph(x + 16, 192, d.wx.day[i].code, true, 24);
    char hi[8], lo[8];
    fmt_temp(hi, sizeof(hi), d.wx.day[i].hi_x10);
    fmt_temp(lo, sizeof(lo), d.wx.day[i].lo_x10);
    gfx_text_m(&AF_LABEL, x + 40, 186, hi, H_TXT);
    gfx_text_m(&AF_BODY_S, x + 40, 203, lo, H_DIM);
  }
}

// ------------------------------------------------------------------ F6 --
static void f6_ring(float t) {  // v3.1.2: static matte ring (the animated aurora-gradient ring is gone)
  PROF_SCOPE(PL_FACE);
  (void)t;
  gfx_ring(F6_CX, F6_CY, F6_R, 2, blend565(C_BG, A_HOME, 120));
}
static void f6_hand(float ang_deg, const Hand &h, float t) {
  PROF_SCOPE(PL_FACE);
  (void)t;
  const float a = (ang_deg - 90) * 0.0174533f;
  const float cx = F6_CX, cy = F6_CY;
  const float x1 = cx + gfx_fcos(a) * h.len, y1 = cy + gfx_fsin(a) * h.len;
  const float x0 = cx - gfx_fcos(a) * h.tail, y0 = cy - gfx_fsin(a) * h.tail;
  const uint16_t c = blend565(k_aur[h.aur], 0xFFFF, 89);
  gfx_line(x0, y0, x1, y1, h.width + 4.0f, k_aur[h.aur], 60);
  gfx_line(x0, y0, x1, y1, (float)h.width, c);
}
static float angdist(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180 ? 360 - d : d;
}
static void face_f6(void) {
  const float t = g_ui.now / 1000.0f;
  // v3.1.2: matte C_BG background, no particles (aurora is boot-only)
  if (!gfx_vis(F6_CY - F6_R - 4, 2 * F6_R + 8)) return;
  f6_ring(t);
  for (int k = 0; k < 60; k++) {
    const float a = (k * 6 - 90) * 0.0174533f, ca = gfx_fcos(a), sa = gfx_fsin(a);
    if (k % 5 == 0) {
      gfx_line(F6_CX + ca * (F6_R - 12), F6_CY + sa * (F6_R - 12), F6_CX + ca * (F6_R - 4), F6_CY + sa * (F6_R - 4), 3,
               k % 15 == 0 ? H_TXT : H_MUT);
    } else {
      gfx_line(F6_CX + ca * (F6_R - 7), F6_CY + sa * (F6_R - 7), F6_CX + ca * (F6_R - 4), F6_CY + sa * (F6_R - 4), 1,
               blend565(C_BG, H_DIM, 140));
    }
  }
  static const char *const num[4] = {"12", "3", "6", "9"};
  for (int i = 0; i < 4; i++) {
    const float a = (i * 90 - 90) * 0.0174533f;
    gfx_text_m(&AF_ROW, (int)(F6_CX + cosf(a) * 70), (int)(F6_CY + sinf(a) * 70), num[i], H_MUT, GA_C);  // libm: exact v3.1.0 placement (4 calls)
  }
  const int hh = g_ui.tv ? g_ui.lt.tm_hour : 10, mm = g_ui.tv ? g_ui.lt.tm_min : 10;
  const float ss = g_ui.tv ? g_ui.lt.tm_sec + (g_ui.now % 1000) / 1000.0f : 0;
  const float ha = (hh % 12 + mm / 60.0f) * 30, ma = (mm + ss / 60.0f) * 6, sa = ss * 6;
  f6_hand(ha, F6_HOUR, t);
  f6_hand(ma, F6_MIN, t);
  // chip placement
  int best = 0;
  float bs = -1e9f;
  for (int i = 0; i < 4; i++) {
    const float sc = fminf(angdist(F6_SLOTS[i].ang, ha), angdist(F6_SLOTS[i].ang, (float)(mm * 6))) - F6_SLOTS[i].penalty;
    if (sc > bs) { bs = sc; best = i; }
  }
  const int ccx = F6_CX + F6_SLOTS[best].dx, ccy = F6_CY + F6_SLOTS[best].dy;
  char tb[8];
  time_str(tb, sizeof(tb));
  gfx_rrect(ccx - F6_CHIP_W / 2, ccy - F6_CHIP_H / 2, F6_CHIP_W, F6_CHIP_H, F6_CHIP_R, H_SURF);
  gfx_rrect_frame(ccx - F6_CHIP_W / 2, ccy - F6_CHIP_H / 2, F6_CHIP_W, F6_CHIP_H, F6_CHIP_R, blend565(H_SURF, H_LINE, 160));
  gfx_text_m(&AF_LABEL, ccx, ccy, tb, H_TXT, GA_C);
  if (g_ui.tv) {
    char db[12], wd[4] = {0};
    memcpy(wd, k_wd[g_ui.lt.tm_wday % 7], 3);
    snprintf(db, sizeof(db), "%s %d", wd, g_ui.lt.tm_mday);
    gfx_micro(ccx, best == 0 ? ccy - 24 : ccy + 13, db, H_DIM, GA_C, true);
  }
  f6_hand(sa, F6_SEC, t);
  gfx_circle(F6_CX, F6_CY, 5, H_TXT);
  gfx_circle(F6_CX, F6_CY, 2, AUR2);
}

// --------------------------------------------------------------- screen --
static void draw_face(uint8_t f) {
  switch (f) {
    case 0: face_f1(); break;
    case 1: face_f2(); break;
    case 2: face_f5(); break;
    default: face_f6(); break;
  }
}
static void home_caches_free(void) {
  tile_cache_free();
  gfx_hero_free(&s_hero[0]);
  gfx_hero_free(&s_hero[1]);
  s_tc_failed = false;
}
uint32_t home_cache_bytes(void) {
  return (s_tc_p ? (uint32_t)TC_BYTES : 0) + gfx_hero_bytes(&s_hero[0]) + gfx_hero_bytes(&s_hero[1]);
}
uint32_t home_release_caches(void) {
  const uint32_t n = home_cache_bytes();
  home_caches_free();
  return n;
}
static void face_step(int dir);
void home_face_step(int dir) { face_step(dir); }
void home_face_set(uint8_t idx) {
  if (idx > 3 || idx == g_ui.prefs.face) return;
  face_step((int)idx - (int)g_ui.prefs.face);
}
static void home_enter(uint32_t now) {
  s_enter_ms = now;
  s_slide_dir = 0;
  s_menu_at = 0;
  buddy_rig_init(&g_rig, BRM_F2, now);
  buddy_place(&g_rig, F2_BX, F2_BY, F2_BS);
}
static void home_leave(void) { home_caches_free(); s_menu_at = 0; }
static void home_tick(uint32_t now) {
  if (s_menu_at && (int32_t)(now - s_menu_at) >= 0) { s_menu_at = 0; ui_go(SCR_MENU); return; }
  // caches: build the one the visible face(s) need, drop the others once a slide is over
  const bool sliding = s_slide_dir && (uint32_t)(now - s_slide_t0) < SLIDE_MS;
  const uint8_t f = g_ui.prefs.face, pf = sliding ? s_prev_face : f;
  // v3.1.3: the 24.6 KB F1 tile sprite is gone (band keys: flap bands are only recomposed
  // during a flip and on the colon window). tile_cache_build() stays for HOME_TILE_SPRITE=1.
#if HOME_TILE_SPRITE
  if (f == 0 || pf == 0) tile_cache_build();
  else if (s_tc_p) { tile_cache_free(); s_tc_failed = false; }
#else
  (void)pf;
#endif
  if (f == 1 || pf == 1) buddy_tick(&g_rig, now);  // F2 rig: frame key + dirty rect for the band keys
  if (f != 1 && pf != 1) gfx_hero_free(&s_hero[0]);
  if (f != 2 && pf != 2) gfx_hero_free(&s_hero[1]);
  char d[5];
  if (g_ui.tv) snprintf(d, sizeof(d), "%02d%02d", g_ui.lt.tm_hour, g_ui.lt.tm_min);
  else memcpy(d, "----", 5);
  for (int i = 0; i < 4; i++)
    if (d[i] != s_digits[i]) {
      s_old[i] = s_digits[i];
      s_digits[i] = d[i];
      s_flip_t0[i] = now + (uint32_t)(i * 60);  // small stagger, like the mockup
      if (s_flip_t0[i] == 0) s_flip_t0[i] = 1;
    }
}
static void face_step(int dir) {
  dir = dir > 0 ? (dir > 3 ? 3 : dir) : (dir < -3 ? -3 : dir);
  s_prev_face = g_ui.prefs.face;
  g_ui.prefs.face = (uint8_t)((g_ui.prefs.face + 4 + dir) % 4);
  s_slide_dir = (int8_t)(dir > 0 ? 1 : -1);
  s_slide_t0 = g_ui.now;
  s_enter_ms = g_ui.now;
  ui_prefs_changed();
}
static void home_input(const UiIn &in, uint32_t now) {
  if (g_ui.prefs.face == 1 && (in.type == IN_CLICK || in.type == IN_TAP)) {
    // F2: pop surprised (tap) / happy (press) on the next frame, menu at +250 ms; a second
    // press or tap inside the wait opens it at once.
    if (s_menu_at) { s_menu_at = 0; ui_go(SCR_MENU); return; }
    buddy_input(&g_rig, in.type == IN_TAP ? BI_TAP : BI_PRESS, 0, now);
    hal_lat_tag(in.type == IN_TAP ? "f2-surprised" : "f2-happy");
    s_menu_at = now + F2_MENU_DELAY_MS;
    if (!s_menu_at) s_menu_at = 1;
    return;
  }
  switch (in.type) {
    case IN_ROT:
      if (g_ui.prefs.face == 1) buddy_input(&g_rig, BI_ROT, in.d, now);  // glance as the face switches
      face_step(in.d > 0 ? 1 : -1);
      break;
    case IN_SWL: face_step(1); break;
    case IN_SWR: face_step(-1); break;
    case IN_CLICK: case IN_TAP: ui_go(SCR_MENU); break;
    case IN_SWU: ui_go(SCR_MENU); break;
    default: break;
  }
}
static void home_draw(void) {
  gfx_fill(C_BG);
  const uint32_t el = g_ui.now - s_slide_t0;
  gfx_clip(0, 24, GFX_W, GFX_H);
  if (s_slide_dir && el < SLIDE_MS) {
    const float e = ease_out(el / (float)SLIDE_MS);
    const int off = (int)(320 * e) * s_slide_dir;
    gfx_origin(-off, 0);
    gfx_clip(0, 24, GFX_W, GFX_H);
    draw_face(s_prev_face);
    gfx_origin(s_slide_dir * 320 - off, 0);
    gfx_clip(0, 24, GFX_W, GFX_H);
    if (gfx_vis(24, 216)) gfx_rect(0, 24, GFX_W, 216, C_BG);
    draw_face(g_ui.prefs.face);
    gfx_origin(0, 0);
  } else {
    draw_face(g_ui.prefs.face);
  }
  gfx_noclip();
  w_page_dots(g_ui.prefs.face, 4, HOME_DOTS_Y, A_HOME, blend565(C_BG, H_MUT, 110));
  w_status("HOME", "", true);
}

// ---------------------------------------------------------- band keys (v3.1.3) --
// Content key per 20-row band; ui_task skips compose + SPI for a band whose key did not
// change. 0 = the band animates this frame. Verified on the host (make bandtest): a band
// whose key repeats must render pixel-identical.
static uint32_t hash_bytes(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
static bool bands_hit(int b, int y0, int y1) { return b * 20 < y1 && b * 20 + 20 > y0; }  // rows [y0,y1)
static uint32_t home_band_key(int b, BandWin *w) {
  const uint32_t now = g_ui.now;
  if (s_slide_dir && (uint32_t)(now - s_slide_t0) < SLIDE_MS) return 0;
  const AuraData &d = g_ui.d;
  const uint8_t f = g_ui.prefs.face;
  uint32_t k = ui_hash(w_status_key(true), f);
  k = ui_hash(k, g_ui.tv ? 1u : 0u);
  if (g_ui.tv) {
    k = ui_hash(k, (uint32_t)(g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min));
    k = ui_hash(k, (uint32_t)(g_ui.lt.tm_mday * 64 + g_ui.lt.tm_mon * 8 + g_ui.lt.tm_wday));
  }
  k = hash_bytes(k, &d.wx, sizeof(d.wx));
  k = hash_bytes(k, &d.air, sizeof(d.air));
  k = hash_bytes(k, d.loc.city, sizeof(d.loc.city));
  if (b == 0) return ui_hash(w_status_key(true), 0xB0u);  // rows 0..19: status bar only
  switch (f) {
    case 0: {  // F1 flaps + colon + seconds bar + gauges
      for (int i = 0; i < 4; i++)
        if (s_flip_t0[i] && (int32_t)(now - s_flip_t0[i]) < (int32_t)FLIP_MS && bands_hit(b, 24, 136)) return 0;
      if (bands_hit(b, 56, 96)) {  // colon blink: window over the colon column only
        w->x0 = COLON_X - COLON_R - 3;
        w->x1 = COLON_X + COLON_R + 4;
        w->key = (!g_ui.tv || (now % 1000) < 500) ? 1u : 2u;
      }
      if (bands_hit(b, SEC_Y - 3, SEC_Y + 4) && g_ui.tv) {
        const float frac = (g_ui.lt.tm_sec + (now % 1000) / 1000.0f) / 60.0f;
        k = ui_hash(k, (uint32_t)(int)((SEC_X1 - SEC_X0) * frac));  // same px math as face_f1
      }
      const bool gauges_live = (uint32_t)(now - s_enter_ms) < GAUGE_COUNTUP_MS + 80u || d.wx.valid || d.air.valid;
      if (gauges_live && bands_hit(b, GAUGE_CY - GAUGE_R - 10, GAUGE_CY + GAUGE_R + 20)) return 0;
      return k;
    }
    case 1:  // F2: v3.1.4 only the rig's dirty rect (last | this frame bounds) is recomposed,
             // and nothing at all while the pose is unchanged; hero time, info and dots are static
      return buddy_band_key(&g_rig, b, w, k);
    case 2: {  // F5: rain/snow particles move over the whole face; the glyph animates
      const uint8_t kind = d.wx.valid ? wx_kind(d.wx.code) : 1;
      if (kind == 2 || kind == 3 || kind == 4) return 0;
      return bands_hit(b, 24, 100) ? 0 : k;
    }
    default:  // F6: the sweeping second hand crosses every band of the dial
      return bands_hit(b, F6_CY - F6_R - 4, F6_CY + F6_R + 4) ? 0 : k;
  }
}

const Screen SCR_HOME_DEF = {"HOME", home_enter, home_leave, home_input, home_tick, home_draw, true, false, home_band_key, nullptr};

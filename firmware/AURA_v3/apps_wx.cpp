// apps_wx.cpp - Weather, Forecast, Sky, Air (Open-Meteo data from net_task).
#include "apps.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const k_wd3[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static void deg_ring(int x, int y, int r, uint16_t c) { gfx_ring((float)x, (float)y, (float)r, 1.6f, c); }
// Big temperature: AA digits (no minus/degree glyphs in the 46 px font) + drawn sign and degree ring.
static int big_temp(int x, int baseline, int16_t x10, uint16_t c) {
  const int v = x10 >= 0 ? (x10 + 5) / 10 : -((-x10 + 5) / 10);
  char b[8];
  snprintf(b, sizeof(b), "%d", v < 0 ? -v : v);
  const int ch = gfx_aacap_h(FAA_B46D);
  if (v < 0) { gfx_rrect(x, baseline - ch / 2 - 2, 12, 5, 2, c); x += 16; }
  gfx_aatext(FAA_B46D, x + 1, baseline + 2, b, C_SHADOW);
  const int xe = gfx_aatext(FAA_B46D, x, baseline, b, c);
  deg_ring(xe + 7, baseline - ch + 4, 4, c);
  return xe + 12;
}
static void unit_text(int x, int ym, const char *s) { gfx_micro(x, ym - 4, s, C_DIM, GA_L, true); }

// ---------------------------------------------------------------- Weather --
void weather_enter(uint32_t) {}
void weather_leave(void) {}
void weather_input(const UiIn &in, uint32_t) {
  if (in.type == IN_CLICK) { hal_net_refresh(JOB_WX); ui_toast("Refreshing weather", 1200); }
}
void weather_draw(void) {
  gfx_fill(C_BG);
  w_status("WEATHER");
  const AuraData &d = g_ui.d;
  if (!w_job_gate(JOB_WX, d.wx.valid)) return;
  const WxData &w = d.wx;
  if (gfx_vis(24, 106)) {
    if (w.is_day) gfx_vgrad(0, 24, GFX_W, 104, RGB565C(44, 96, 178), C_BG);
    else gfx_vgrad(0, 24, GFX_W, 104, RGB565C(26, 34, 76), C_BG);
    // condition glyph (A4, 48 px where the asset has it, else 32) with a soft halo
    if (wx_kind(w.code) == 0 && w.is_day) gfx_circle(256, 53, 26, blend565(RGB565C(44, 96, 178), SUN_C, 40));
    w_wx_glyph(256, 53, w.code, w.is_day, 48, w.wind_x10);
    big_temp(22, 92, w.temp_x10, C_TXT);
    char b[64], f[10], hi[8], lo[8];
    gfx_text_m(&AF_TITLE, 104, 64, wx_text(w.code), C_TXT);
    fmt_temp(f, sizeof(f), w.feels_x10);
    if (w.ndays) {
      snprintf(hi, sizeof(hi), "%d", (w.day[0].hi_x10 + 5) / 10);
      snprintf(lo, sizeof(lo), "%d", (w.day[0].lo_x10 + 5) / 10);
      snprintf(b, sizeof(b), "FEELS %s  " AF_DOT "  H%s L%s", f, hi, lo);
    } else snprintf(b, sizeof(b), "FEELS %s", f);
    gfx_micro(104, 81, b, C_TXT);
    // Q07: stale age lives in the status-bar chip (C_WARN), not over the hero
  }
  // metric cards
  static const char *const lbl[3] = {"HUMIDITY", "WIND", "UV"};
  for (int i = 0; i < 3; i++) {
    const int x = 12 + i * 101;
    w_card(x, 136, 95, 54, 8);
    if (!gfx_vis(136, 54)) continue;
    gfx_micro(x + 10, 145, lbl[i], C_DIM);
    char v[8];
    const char *u;
    if (i == 0) { snprintf(v, sizeof(v), "%u", (unsigned)w.hum); u = "%"; }
    else if (i == 1) { snprintf(v, sizeof(v), "%u", (unsigned)((w.wind_x10 + 5) / 10)); u = "KM/H"; }
    else {
      snprintf(v, sizeof(v), "%u", (unsigned)((w.uv_x10 + 5) / 10));
      const int uv = (w.uv_x10 + 5) / 10;
      u = uv < 3 ? "LOW" : uv < 6 ? "MOD" : uv < 8 ? "HIGH" : uv < 11 ? "V.HIGH" : "EXTR";
    }
    const int xe = gfx_text_m(&AF_NUM_M, x + 10, 172, v, C_TXT);
    unit_text(xe + gfx_text_w(&AF_NUM_M, v) + 4, 174, u);
  }
  // sunrise -> sunset progress
  if (gfx_vis(200, 24)) {
    gfx_hline(12, 205, 296, C_LINE);
    char a[16], b2[16], t1[8], t2[8];
    fmt_hm(t1, sizeof(t1), w.sunrise_min);
    fmt_hm(t2, sizeof(t2), w.sunset_min);
    snprintf(a, sizeof(a), "RISE %s", t1);
    snprintf(b2, sizeof(b2), "SET %s", t2);
    gfx_micro(12, 212, a, C_MUT);
    gfx_micro(308, 212, b2, C_MUT, GA_R);
    const int x0 = 90, x1 = 230;
    gfx_hline(x0, 216, x1 - x0, C_LINE);
    const bool stale = job_age_min(JOB_WX) >= 10 && job_age_min(JOB_WX) != 0xFFFFFFFFu;
    const uint16_t bc = stale ? C_WARN : C_ACC;
    if (w.sunrise_min >= 0 && w.sunset_min > w.sunrise_min && g_ui.tv) {
      const int nowm = g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min;
      const float f = ui_clampf((nowm - w.sunrise_min) / (float)(w.sunset_min - w.sunrise_min), 0, 1);
      const int px = x0 + (int)((x1 - x0) * f);
      gfx_rect(x0, 215, px - x0, 3, bc);
      gfx_circle((float)px, 216.5f, 3, bc);
    }
  }
}

// --------------------------------------------------------------- Forecast --
void forecast_enter(uint32_t) {}
void forecast_leave(void) {}
void forecast_input(const UiIn &in, uint32_t) {
  if (in.type == IN_CLICK) { hal_net_refresh(JOB_WX); ui_toast("Refreshing forecast", 1200); }
}
void forecast_draw(void) {
  gfx_fill(C_BG);
  w_status("FORECAST");
  const AuraData &d = g_ui.d;
  if (!w_job_gate(JOB_WX, d.wx.valid && d.wx.ndays > 0)) return;
  const WxData &w = d.wx;
  // next 12 h curve
  if (gfx_vis(28, 56) && w.nh >= 2) {
    gfx_micro(14, 32, "NEXT 12 H", C_DIM);
    int mn = 9999, mx = -9999;
    for (int i = 0; i < w.nh; i++) { if (w.hour_x10[i] < mn) mn = w.hour_x10[i]; if (w.hour_x10[i] > mx) mx = w.hour_x10[i]; }
    if (mx - mn < 20) { mx += 10; mn -= 10; }
    float xy[24];
    for (int i = 0; i < w.nh; i++) {
      xy[2 * i] = 20.0f + i * (276.0f / (w.nh - 1));
      xy[2 * i + 1] = 78.0f - (w.hour_x10[i] - mn) * 16.0f / (mx - mn);
    }
    gfx_polyline(xy, w.nh, 1.4f, C_ACC);
    for (int i = 0; i < w.nh - 1; i += 3) {
      gfx_circle(xy[2 * i], xy[2 * i + 1], 3, C_ACC);
      char b[6];
      snprintf(b, sizeof(b), "%d", (w.hour_x10[i] + 5) / 10);
      gfx_micro((int)xy[2 * i], (int)xy[2 * i + 1] - 12, b, C_MUT, GA_C, true);
    }
  }
  w_stale_pill(JOB_WX, -1, 30);
  int mn = 9999, mx = -9999;
  for (int i = 0; i < w.ndays; i++) { if (w.day[i].lo_x10 < mn) mn = w.day[i].lo_x10; if (w.day[i].hi_x10 > mx) mx = w.day[i].hi_x10; }
  if (mx <= mn) mx = mn + 10;
  for (int i = 0; i < w.ndays && i < 5; i++) {
    const int y = 88 + i * 27;
    if (!gfx_vis(y, 24)) continue;
    w_card(10, y, 300, 23, 5, i == 0);
    const int cy = y + 12;
    gfx_micro(20, cy - 4, i == 0 ? "TODAY" : k_wd3[(g_ui.lt.tm_wday + i) % 7], i == 0 ? C_TXT : C_MUT);
    w_wx_glyph(76, cy, w.day[i].code, true, 24);
    char lo[8], hi[8];
    fmt_temp(lo, sizeof(lo), w.day[i].lo_x10);
    fmt_temp(hi, sizeof(hi), w.day[i].hi_x10);
    gfx_micro(112, cy - 4, lo, C_DIM);
    const int bx0 = 170, bx1 = 262;
    gfx_rrect(bx0, cy - 2, bx1 - bx0, 4, 2, C_TILE_HI);
    const int a = bx0 + (w.day[i].lo_x10 - mn) * (bx1 - bx0) / (mx - mn), b = bx0 + (w.day[i].hi_x10 - mn) * (bx1 - bx0) / (mx - mn);
    for (int x = a; x < b; x++) gfx_rect(x, cy - 2, 1, 4, blend565(RGB565C(96, 172, 226), SUN_C, (uint8_t)((x - bx0) * 255 / (bx1 - bx0))));
    gfx_micro(300, cy - 4, hi, C_TXT, GA_R);
    if (w.day[i].pop >= 30) {
      char p[8];
      snprintf(p, sizeof(p), "%u%%", (unsigned)w.day[i].pop);
      gfx_micro(140, cy - 4, p, RAIN_C, GA_L, true);
    }
  }
}

// -------------------------------------------------------------------- Sky --
static float moon_phase(void) {
  const double days = (double)time(nullptr) / 86400.0 - 10962.7597;  // since 2000-01-06 18:14 UTC
  double p = fmod(days / 29.530588853, 1.0);
  if (p < 0) p += 1;
  return (float)p;
}
static const char *moon_name(float p) {
  if (p < 0.03f || p > 0.97f) return "NEW MOON";
  if (p < 0.22f) return "WAXING CRESCENT";
  if (p < 0.28f) return "FIRST QUARTER";
  if (p < 0.47f) return "WAXING GIBBOUS";
  if (p < 0.53f) return "FULL MOON";
  if (p < 0.72f) return "WANING GIBBOUS";
  if (p < 0.78f) return "LAST QUARTER";
  return "WANING CRESCENT";
}
void sky_enter(uint32_t) {}
void sky_leave(void) {}
void sky_input(const UiIn &, uint32_t) {}
void sky_draw(void) {
  gfx_fill(C_BG);
  const AuraData &d = g_ui.d;
  const WxData &w = d.wx;
  const bool have = d.wx.valid && w.sunrise_min >= 0 && w.sunset_min > w.sunrise_min;
  if (!have) {
    w_status("SKY");
    w_job_gate(JOB_WX, false);
    return;
  }
  gfx_vgrad(0, 24, GFX_W, 130, RGB565C(22, 22, 48), RGB565C(58, 40, 74));
  gfx_vgrad(0, 154, GFX_W, 86, RGB565C(44, 30, 38), C_BG);
  if (gfx_vis(153, 2)) gfx_hline(0, 154, GFX_W, RGB565C(130, 86, 64));
  const float cx = 160, cy = 154, rx = 120, ry = 110;
  if (gfx_vis(40, 116)) {
    float xy[2 * 41];
    for (int i = 0; i <= 40; i++) {
      const float a = 3.14159f * (1 - i / 40.0f);
      xy[2 * i] = cx + cosf(a) * rx;
      xy[2 * i + 1] = cy - sinf(a) * ry;
    }
    gfx_polyline(xy, 41, 1.3f, RGB565C(70, 128, 186));
    for (int k = 1; k < 6; k++) {
      const float a = 3.14159f * (1 - k / 6.0f);
      gfx_circle(cx + cosf(a) * rx, cy - sinf(a) * ry, 1.3f, RGB565C(110, 160, 210));
    }
    const int nowm = g_ui.tv ? g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min : 12 * 60;
    if (nowm >= w.sunrise_min && nowm <= w.sunset_min) {
      const float f = (nowm - w.sunrise_min) / (float)(w.sunset_min - w.sunrise_min);
      const float a = 3.14159f * (1 - f);
      const float sx = cx + cosf(a) * rx, sy = cy - sinf(a) * ry;
      gfx_circle(sx, sy, 16, RGB565C(92, 80, 84));
      gfx_circle(sx, sy, 11, RGB565C(150, 120, 90));
      gfx_circle(sx, sy, 8, SUN_C);
    } else {
      gfx_micro(160, 120, "THE SUN HAS SET", C_DIM, GA_C);
    }
  }
  char b[32], t1[8], t2[8];
  fmt_hm(t1, sizeof(t1), w.sunrise_min);
  fmt_hm(t2, sizeof(t2), w.sunset_min);
  snprintf(b, sizeof(b), "RISE %s", t1);
  gfx_micro(14, 161, b, C_MUT);
  snprintf(b, sizeof(b), "SET %s", t2);
  gfx_micro(306, 161, b, C_MUT, GA_R);
  fmt_hm(t1, sizeof(t1), w.sunset_min - 41);
  snprintf(b, sizeof(b), "GOLDEN HOUR %s", t1);
  gfx_micro(14, 177, b, C_ACC);
  const int dl = w.sunset_min - w.sunrise_min;
  snprintf(b, sizeof(b), "DAYLIGHT %dH %02dM", dl / 60, dl % 60);
  gfx_micro(14, 193, b, C_DIM);
  const float ph = moon_phase();
  w_moon(272, 196, 16, ph, C_BG);
  gfx_micro(250, 222, moon_name(ph), C_MUT, GA_C);
  w_status("SKY");
}

// -------------------------------------------------------------------- Air --
static const char *const k_aqi_name[6] = {"GOOD", "MODERATE", "SENSITIVE", "UNHEALTHY", "V. UNHEALTHY", "HAZARDOUS"};
static int aqi_band(int aqi) {
  int i = 0;
  while (i < 5 && aqi > AQI_BREAKS[i + 1]) i++;
  return i;
}
static float aqi_pos(int aqi) {  // 0..6, equal width per band
  const int i = aqi_band(aqi);
  const float f = ui_clampf((aqi - AQI_BREAKS[i]) / (float)(AQI_BREAKS[i + 1] - AQI_BREAKS[i]), 0, 1);
  return i + f;
}
void air_enter(uint32_t) {}
void air_leave(void) {}
void air_input(const UiIn &in, uint32_t) {
  if (in.type == IN_CLICK) { hal_net_refresh(JOB_AIR); ui_toast("Refreshing air quality", 1200); }
}
void air_draw(void) {
  gfx_fill(C_BG);
  w_status("AIR");
  const AuraData &d = g_ui.d;
  if (!w_job_gate(JOB_AIR, d.air.valid)) return;
  const AirData &a = d.air;
  const int band = aqi_band(a.aqi);
  const uint16_t bc = AQI_COL[band];
  const bool live = job_age_min(JOB_AIR) < 10;
  gfx_circle(24, 35, 3, live ? C_DN : C_DIM);
  gfx_micro(32, 31, live ? "LIVE" : "CACHED", live ? C_TXT : C_WARN);
  // segmented arc gauge
  const float gcx = 95, gcy = 128, R = 66, A0 = 120, SW = 300;
  for (int i = 0; i < 6; i++) {
    const float a0 = A0 + SW * i / 6 + 1.5f, a1 = A0 + SW * (i + 1) / 6 - 1.5f;
    gfx_arc(gcx, gcy, R, 7, a0, a1, blend565(C_BG, AQI_COL[i], 80));
  }
  const float pos = aqi_pos(a.aqi);
  gfx_arc(gcx, gcy, R, 7, A0, A0 + SW * pos / 6, bc);
  {
    const float am = (A0 + SW * pos / 6) * 0.0174533f;
    gfx_line(gcx + cosf(am) * (R - 11), gcy + sinf(am) * (R - 11), gcx + cosf(am) * (R + 3), gcy + sinf(am) * (R + 3), 2, C_TXT);
  }
  char v[8];
  snprintf(v, sizeof(v), "%u", (unsigned)a.aqi);
  gfx_aatext_hero(FAA_B34N, 118, 124, v, bc, C_BG, bc, 0);
  gfx_micro(118, 152, "US AQI", C_DIM, GA_C);
  // right column
  gfx_micro(190, 40, "AIR QUALITY IS", C_MUT);
  {
    const int w = gfx_micro_w(k_aqi_name[band]) + 20;
    gfx_rrect(190, 49, w, 18, 9, blend565(C_BG, bc, 70));
    gfx_micro(200, 54, k_aqi_name[band], C_TXT);
  }
  char b[12];
  gfx_micro(190, 86, "PM 2.5", C_MUT);
  snprintf(b, sizeof(b), "%u", (unsigned)((a.pm25_x10 + 5) / 10));
  gfx_text_m(&AF_NUM_M, 190, 106, b, C_TXT);
  unit_text(242, 108, "UG/M3");
  gfx_micro(190, 132, "PM 10", C_MUT);
  snprintf(b, sizeof(b), "%u", (unsigned)((a.pm10_x10 + 5) / 10));
  gfx_text_m(&AF_NUM_M, 190, 152, b, C_TXT);
  unit_text(242, 154, "UG/M3");
  // dominant pollutant by US AQI sub-index ratio (PM2.5 35.4 / PM10 154 at the 100 mark)
  const bool pm10_dom = a.pm10_x10 / 154.0f > a.pm25_x10 / 35.4f;
  gfx_micro(190, 182, pm10_dom ? "PM10 DOMINANT" : "PM2.5 DOMINANT", C_MUT);
  // 0..500 band bar
  if (gfx_vis(205, 30)) {
    for (int i = 0; i < 6; i++) {
      const int x0 = 16 + i * 48, w = 46;
      gfx_rect(x0, 212, w, 4, blend565(C_BG, AQI_COL[i], 150));
    }
    const int mx = 16 + (int)(pos * 48);
    gfx_rect(mx, 208, 1, 12, C_TXT);
    gfx_micro(18, 225, "0", C_DIM, GA_L, true);
    gfx_micro(160, 225, "150", C_DIM, GA_C, true);
    gfx_micro(302, 225, "500", C_DIM, GA_R, true);
  }
}

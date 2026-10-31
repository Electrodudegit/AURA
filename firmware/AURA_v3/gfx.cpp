// gfx.cpp - band renderer (see gfx.h). No heap, no Arduino.
#include "prof.h"
#include "gfx.h"
#include "font5x7.h"
#include "aura_fonts.h"
#include <math.h>
#include <string.h>

int g_gfx_y0 = 0, g_gfx_y1 = 0;
static uint16_t *s_buf = nullptr;
static int s_by0 = 0, s_bh = 0;                  // band, screen rows
static int s_bw = GFX_W;                         // buffer width (stride); GFX_W except cache builds
static int s_wx = 0;                             // v3.1.3: screen x of buffer column 0 (window compose)
static int s_cx0 = 0, s_cy0 = 0, s_cx1 = GFX_W, s_cy1 = GFX_H;  // clip, screen coords
static int s_ux0 = 0, s_uy0 = 0, s_ux1 = GFX_W, s_uy1 = GFX_H;  // user clip
static int s_ox = 0, s_oy = 0;

static inline uint16_t sw(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  const int r1 = (a >> 11) & 31, g1 = (a >> 5) & 63, b1 = a & 31;
  const int r2 = (b >> 11) & 31, g2 = (b >> 5) & 63, b2 = b & 31;
  // v3.1.1: no divides (t + t>>7 maps 0..255 onto 0..256, endpoints exact)
  const int k = t + (t >> 7);
  const int r = r1 + (((r2 - r1) * k) >> 8), g = g1 + (((g2 - g1) * k) >> 8), bb = b1 + (((b2 - b1) * k) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bb);
}
uint16_t dim565(uint16_t c, uint16_t k) {
  const uint32_t r = (((c >> 11) & 31) * k) >> 8, g = (((c >> 5) & 63) * k) >> 8, b = ((c & 31) * k) >> 8;
  return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
}

static void recalc(void) {
  s_cx0 = s_ux0 < s_wx ? s_wx : s_ux0;
  s_cx1 = s_ux1 > s_wx + s_bw ? s_wx + s_bw : s_ux1;
  s_cy0 = s_uy0 < s_by0 ? s_by0 : s_uy0;
  s_cy1 = s_uy1 > s_by0 + s_bh ? s_by0 + s_bh : s_uy1;
  if (s_cy1 < s_cy0) s_cy1 = s_cy0;
  g_gfx_y0 = s_cy0 - s_oy;
  g_gfx_y1 = s_cy1 - s_oy;
}

void gfx_begin(uint16_t *buf, int y0, int h) { gfx_begin_ex(buf, y0, h, GFX_W); }
void gfx_begin_ex(uint16_t *buf, int y0, int h, int w) { gfx_begin_win(buf, 0, y0, w, h); }
void gfx_begin_win(uint16_t *buf, int x0, int y0, int w, int h) {
  s_wx = x0;
  s_bw = w;
  s_buf = buf;
  s_by0 = y0;
  s_bh = h;
  s_ox = s_oy = 0;
  s_ux0 = 0; s_uy0 = 0; s_ux1 = GFX_W; s_uy1 = GFX_H;
  recalc();
}
int gfx_y0(void) { return g_gfx_y0; }
int gfx_y1(void) { return g_gfx_y1; }
void gfx_clip(int x0, int y0, int x1, int y1) {
  s_ux0 = x0 + s_ox; s_uy0 = y0 + s_oy; s_ux1 = x1 + s_ox; s_uy1 = y1 + s_oy;
  recalc();
}
void gfx_noclip(void) {
  s_ux0 = 0; s_uy0 = 0; s_ux1 = GFX_W; s_uy1 = GFX_H;
  recalc();
}
void gfx_origin(int dx, int dy) {
  // keep the user clip in screen space
  s_ox = dx;
  s_oy = dy;
  recalc();
}
int gfx_ox(void) { return s_ox; }
int gfx_oy(void) { return s_oy; }

// ---- raw access (screen coords, already clipped) ----
static inline uint16_t *P(int sx, int sy) { return s_buf + (sy - s_by0) * s_bw + (sx - s_wx); }
static inline void put(int sx, int sy, uint16_t c) { *P(sx, sy) = sw(c); }
static inline void put_a(int sx, int sy, uint16_t c, uint32_t a) {
  if (a >= 255) { *P(sx, sy) = sw(c); return; }
  if (a == 0) return;
  uint16_t *p = P(sx, sy);
  *p = sw(blend565(sw(*p), c, (uint8_t)a));
}
static inline uint32_t mul255(uint32_t k, uint32_t a) { return (k * a + 128 + ((k * a + 128) >> 8)) >> 8; }  // ~ k*a/255
// v3.1.1 packed blend: RGB565 spread to 0x07E0F81F so one multiply blends all 3 channels (5-bit alpha)
#define SPREAD_M 0x07E0F81Fu
static inline uint32_t spread(uint16_t c) { return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_M; }
static inline uint16_t unspread(uint32_t v) { return (uint16_t)((v | (v >> 16)) & 0xFFFF); }
static inline void put_a5(uint16_t *p, uint32_t cs, uint16_t craw_sw, uint32_t a5) {  // a5 0..32
  if (a5 >= 32) { *p = craw_sw; return; }
  const uint32_t b = spread(sw(*p));
  *p = sw(unspread(((cs * a5 + b * (32 - a5)) >> 5) & SPREAD_M));
}
static inline bool in_clip(int sx, int sy) { return sx >= s_cx0 && sx < s_cx1 && sy >= s_cy0 && sy < s_cy1; }

// Intersect a local-coord box with the clip; returns false if empty. Outputs screen coords.
static bool box(int x0, int y0, int x1, int y1, int *X0, int *Y0, int *X1, int *Y1) {
  x0 += s_ox; x1 += s_ox; y0 += s_oy; y1 += s_oy;
  if (x0 < s_cx0) x0 = s_cx0;
  if (y0 < s_cy0) y0 = s_cy0;
  if (x1 > s_cx1) x1 = s_cx1;
  if (y1 > s_cy1) y1 = s_cy1;
  if (x0 >= x1 || y0 >= y1) return false;
  *X0 = x0; *Y0 = y0; *X1 = x1; *Y1 = y1;
  return true;
}
static bool boxf(float x0, float y0, float x1, float y1, int *X0, int *Y0, int *X1, int *Y1) {
  return box((int)floorf(x0), (int)floorf(y0), (int)ceilf(x1) + 1, (int)ceilf(y1) + 1, X0, Y0, X1, Y1);
}

void gfx_fill(uint16_t c) {
  PROF_SCOPE(PL_BG);
  if (!s_buf) return;
  const uint16_t v = sw(c);
  const uint32_t v2 = (uint32_t)v | ((uint32_t)v << 16);
  for (int y = s_cy0; y < s_cy1; y++) {
    uint16_t *p = P(s_cx0, y);
    int n = s_cx1 - s_cx0;
    if (n > 0 && ((uintptr_t)p & 2)) { *p++ = v; n--; }
    uint32_t *q = (uint32_t *)p;
    for (; n >= 2; n -= 2) *q++ = v2;
    if (n) *(uint16_t *)q = v;
  }
}
void gfx_px(int x, int y, uint16_t c) {
  x += s_ox; y += s_oy;
  if (in_clip(x, y)) put(x, y, c);
}
void gfx_px_a(int x, int y, uint16_t c, uint8_t a) {
  x += s_ox; y += s_oy;
  if (in_clip(x, y)) put_a(x, y, c, a);
}
void gfx_px_add(int x, int y, int r5, int g6, int b5) {
  x += s_ox; y += s_oy;
  if (!in_clip(x, y)) return;
  uint16_t *p = P(x, y);
  const uint16_t v = sw(*p);
  int r = ((v >> 11) & 31) + r5, g = ((v >> 5) & 63) + g6, b = (v & 31) + b5;
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  *p = sw((uint16_t)((r << 11) | (g << 5) | b));
}
uint16_t *gfx_rowp(int y, int *lx0, int *lx1, int *ox) {
  const int sy = y + s_oy;
  if (!s_buf || sy < s_cy0 || sy >= s_cy1 || s_cx0 >= s_cx1) return nullptr;
  *lx0 = s_cx0 - s_ox;
  *lx1 = s_cx1 - s_ox;
  *ox = s_ox;
  return s_buf + (sy - s_by0) * s_bw - s_wx;  // index with [lx + ox]
}
uint16_t gfx_get(int x, int y) {
  x += s_ox; y += s_oy;
  if (!in_clip(x, y)) return 0;
  return sw(*P(x, y));
}
void gfx_rect(int x, int y, int w, int h, uint16_t c) {
  int X0, Y0, X1, Y1;
  if (!box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  const uint16_t v = sw(c);
  for (int yy = Y0; yy < Y1; yy++) {
    uint16_t *p = P(X0, yy);
    for (int xx = X0; xx < X1; xx++) *p++ = v;
  }
}
void gfx_rect_a(int x, int y, int w, int h, uint16_t c, uint8_t a) {
  int X0, Y0, X1, Y1;
  if (!box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  for (int yy = Y0; yy < Y1; yy++)
    for (int xx = X0; xx < X1; xx++) put_a(xx, yy, c, a);
}
void gfx_hline(int x, int y, int w, uint16_t c) { gfx_rect(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, uint16_t c) { gfx_rect(x, y, 1, h, c); }
void gfx_frame(int x, int y, int w, int h, uint16_t c) {
  gfx_hline(x, y, w, c);
  gfx_hline(x, y + h - 1, w, c);
  gfx_vline(x, y, h, c);
  gfx_vline(x + w - 1, y, h, c);
}
void gfx_vgrad(int x, int y, int w, int h, uint16_t top, uint16_t bot) {
  PROF_SCOPE(PL_BG);
  int X0, Y0, X1, Y1;
  if (h <= 0 || !box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  for (int yy = Y0; yy < Y1; yy++) {
    const int r = yy - (y + s_oy);
    const uint16_t v = sw(blend565(top, bot, (uint8_t)(h > 1 ? (r * 255) / (h - 1) : 0)));
    uint16_t *p = P(X0, yy);
    for (int xx = X0; xx < X1; xx++) *p++ = v;
  }
}

// ---- fast math ----
// atan2 in degrees, |error| < 0.001 deg (minimax polynomial on [0,1]); no libm call.
float gfx_atan2_deg(float y, float x) {
  const float ax = fabsf(x), ay = fabsf(y);
  if (ax < 1e-9f && ay < 1e-9f) return 0;
  const bool sw2 = ay > ax;
  const float z = sw2 ? ax / ay : ay / ax, z2 = z * z;
  float a = 57.29578f * z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
  if (sw2) a = 90.0f - a;
  if (x < 0) a = 180.0f - a;
  if (y < 0) a = -a;
  return a;
}
// v3.1.4: the 257-entry table is a const in flash (was a 514 B .bss table filled on first use
// with lrintf(16384 * sinf(i * 2pi / 256)); same values, checked: no entry is within 0.005 of
// a rounding boundary, so the libm used to generate it cannot change a value).
static const int16_t s_sin_lut[257] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
    16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
    15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
    11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
    6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
    -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
    -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
    0,
};
int gfx_isin(uint32_t a16) {
  a16 &= 0xFFFF;
  const int i = (int)(a16 >> 8), f = (int)(a16 & 0xFF);
  return s_sin_lut[i] + (((s_sin_lut[i + 1] - s_sin_lut[i]) * f) >> 8);
}
float gfx_fsin(float rad) {
  // rad -> 1/65536 turn, wrapped via integer arithmetic (handles any sign/magnitude seen in the UI)
  const float turns = rad * (1.0f / 6.2831853f);
  const float fl = floorf(turns);
  return gfx_isin((uint32_t)((turns - fl) * 65536.0f)) * (1.0f / 16384.0f);
}

// ---- SDF helpers ----
static inline uint32_t cov(float d) {  // d = signed distance (px), <0 inside
  if (d <= -0.5f) return 255;
  if (d >= 0.5f) return 0;
  return (uint32_t)((0.5f - d) * 255.0f);
}
static inline float sd_rrect(float px, float py, float hx, float hy, float r) {
  // centred box half extents hx, hy, corner radius r
  const float qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
  const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  const float m = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (m < 0 ? m : 0) - r;
}

// v3.1.1 corner coverage tables: for integer rrects the corner coverage depends only on the
// pixel's distance (i, j) from the nearest vertical / horizontal edge and on r, so it is
// computed once per radius (small LRU, r <= CT_RMAX) instead of an SDF + sqrt per pixel.
#define CT_RMAX 20
#define CT_N 4
struct CornerTab { int8_t r; uint8_t frame; uint32_t used; uint8_t k[(CT_RMAX + 1) * CT_RMAX]; };
static CornerTab s_ct[2][CT_N];
static uint32_t s_ct_clock = 0;
static const uint8_t *corner_tab(int r, bool frame) {
  if (r < 1 || r > CT_RMAX) return nullptr;
  CornerTab *t = s_ct[frame ? 1 : 0], *lru = &t[0];
  s_ct_clock++;
  for (int n = 0; n < CT_N; n++) {
    if (t[n].r == r) { t[n].used = s_ct_clock; return t[n].k; }
    if (t[n].used < lru->used) lru = &t[n];
  }
  lru->r = (int8_t)r;
  lru->used = s_ct_clock;
  for (int j = 0; j <= r; j++)
    for (int i = 0; i < r; i++) {
      const float qx = r - i - 0.5f, qy = r - j - 0.5f;
      if (!frame) {
        const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, m = qx > qy ? qx : qy;
        lru->k[j * r + i] = (uint8_t)cov(sqrtf(ox * ox + oy * oy) + (m < 0 ? m : 0) - r);
      } else {
        const float rr = r - 0.5f, ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, m = qx > qy ? qx : qy;
        lru->k[j * r + i] = (uint8_t)cov(fabsf(sqrtf(ox * ox + oy * oy) + (m < 0 ? m : 0) - rr) - 0.5f);
      }
    }
  return lru->k;
}
void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c, uint8_t a) {
  if (w <= 0 || h <= 0) return;
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  int X0, Y0, X1, Y1;
  if (!box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  const float cx = x + s_ox + w * 0.5f, cy = y + s_oy + h * 0.5f, hx = w * 0.5f, hy = h * 0.5f;
  // columns whose |px| <= hx - r are outside the corner arcs: their coverage depends on the row only
  const int sx0 = x + s_ox + r, sx1 = x + s_ox + w - r;  // [sx0, sx1) straight span
  const uint8_t *ct = corner_tab(r, false);
  for (int yy = Y0; yy < Y1; yy++) {
    const float py = yy + 0.5f - cy;
    const bool mid = fabsf(py) < hy - r - 0.5f;  // rows with no corner
    if (mid) {
      if (a >= 255) {
        const uint16_t v = sw(c);
        uint16_t *p = P(X0, yy);
        for (int xx = X0; xx < X1; xx++) *p++ = v;
      } else
        for (int xx = X0; xx < X1; xx++) put_a(xx, yy, c, a);
      continue;
    }
    const uint32_t km = mul255(cov(fabsf(py) - hy), a);  // straight-edge coverage of this row
    for (int xx = X0; xx < X1; xx++) {
      if (xx >= sx0 && xx < sx1) {
        if (km) put_a(xx, yy, c, km);
        continue;
      }
      uint32_t k;
      if (ct) {
        const int lx = xx - s_ox, ly = yy - s_oy;
        const int i = lx - x < x + w - 1 - lx ? lx - x : x + w - 1 - lx;
        const int j = ly - y < y + h - 1 - ly ? ly - y : y + h - 1 - ly;
        k = (i >= 0 && i < r && j >= 0 && j <= r) ? ct[j * r + i] : cov(sd_rrect(xx + 0.5f - cx, py, hx, hy, (float)r));
      } else k = cov(sd_rrect(xx + 0.5f - cx, py, hx, hy, (float)r));
      if (k) put_a(xx, yy, c, mul255(k, a));
    }
  }
}
void gfx_rrect_frame(int x, int y, int w, int h, int r, uint16_t c, uint8_t a) {
  if (w <= 0 || h <= 0) return;
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  int X0, Y0, X1, Y1;
  if (!box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  const float cx = x + s_ox + w * 0.5f, cy = y + s_oy + h * 0.5f, hx = w * 0.5f - 0.5f, hy = h * 0.5f - 0.5f;
  const float rr = r > 0 ? r - 0.5f : 0.f;
  const int sx0 = x + s_ox + r + 1, sx1 = x + s_ox + w - r - 1;  // straight span (|px| <= hx - rr)
  const uint8_t *ct = corner_tab(r, true);
  for (int yy = Y0; yy < Y1; yy++) {
    const float py = yy + 0.5f - cy;
    const bool mid = fabsf(py) < hy - r;
    // straight span coverage depends on the row only: |sd| = | |py| - hy |
    const uint32_t km = mul255(cov(fabsf(fabsf(py) - hy) - 0.5f), a);
    if (mid) {
      // straight rows: only the clip edges and the two frame columns can be covered
      const int cand[4] = {X0, x + s_ox, x + w - 1 + s_ox, X1 - 1};
      for (int i = 0; i < 4; i++) {
        const int xx = cand[i];
        if (xx < X0 || xx >= X1) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) dup |= cand[j] == xx;
        if (dup) continue;
        const float d = fabsf(sd_rrect(xx + 0.5f - cx, py, hx, hy, rr));
        const uint32_t k = cov(d - 0.5f);
        if (k) put_a(xx, yy, c, mul255(k, a));
      }
      continue;
    }
    for (int xx = X0; xx < X1; xx++) {
      if (xx >= sx0 && xx < sx1) {
        if (km) put_a(xx, yy, c, km);
        continue;
      }
      uint32_t k;
      const int lx = xx - s_ox, ly = yy - s_oy;
      const int i = lx - x < x + w - 1 - lx ? lx - x : x + w - 1 - lx;
      const int j = ly - y < y + h - 1 - ly ? ly - y : y + h - 1 - ly;
      if (ct && i >= 0 && i < r && j >= 0 && j <= r) k = ct[j * r + i];
      else k = cov(fabsf(sd_rrect(xx + 0.5f - cx, py, hx, hy, rr)) - 0.5f);
      if (k) put_a(xx, yy, c, mul255(k, a));
    }
  }
}
void gfx_tile(int x, int y, int w, int h, int r, uint16_t top, uint16_t bot, uint16_t rim) {
  if (!gfx_vis(y, h)) return;
  gfx_rrect(x, y, w, h, r, bot);
  gfx_clip(x, y, x + w, y + h / 2);
  gfx_rrect(x, y, w, h, r, top);
  gfx_noclip();
  gfx_hline(x + r, y, w - 2 * r, rim);
}
void gfx_circle(float cx, float cy, float r, uint16_t c, uint8_t a) {
  int X0, Y0, X1, Y1;
  if (!boxf(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &X0, &Y0, &X1, &Y1)) return;
  cx += s_ox; cy += s_oy;
  const float ri = r - 1.5f;  // conservative full-coverage radius (exact cov = 255 inside r - 0.5)
  for (int yy = Y0; yy < Y1; yy++) {
    const float dy = yy + 0.5f - cy;
    // v3.1.1: interior span without sqrt per pixel
    int fa = 1, fb = 0;
    if (ri > 0 && fabsf(dy) < ri) {
      const float hw = sqrtf(ri * ri - dy * dy);
      fa = (int)ceilf(cx - hw - 0.5f);
      fb = (int)floorf(cx + hw - 0.5f);
    }
    for (int xx = X0; xx < X1; xx++) {
      if (xx >= fa && xx <= fb) {
        if (a >= 255) put(xx, yy, c); else put_a(xx, yy, c, a);
        continue;
      }
      const float dx = xx + 0.5f - cx;
      const uint32_t k = cov(sqrtf(dx * dx + dy * dy) - r);
      if (k) put_a(xx, yy, c, mul255(k, a));
    }
  }
}
void gfx_ellipse(float cx, float cy, float rx, float ry, uint16_t c, uint8_t a) {
  if (rx <= 0 || ry <= 0) return;
  int X0, Y0, X1, Y1;
  if (!boxf(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1, &X0, &Y0, &X1, &Y1)) return;
  cx += s_ox; cy += s_oy;
  const float m = rx < ry ? rx : ry, irx = 1.0f / rx, iry = 1.0f / ry;
  const float qi = 1.0f - 1.0f / m;   // conservative normalized full-coverage radius (exact: 1 - 0.5/m)
  const float qo = 1.0f + 1.0f / m;   // conservative normalized zero-coverage radius (exact: 1 + 0.5/m)
  for (int yy = Y0; yy < Y1; yy++) {
    const float dy = (yy + 0.5f - cy) * iry;
    if (fabsf(dy) >= qo) continue;
    const float ho = sqrtf(qo * qo - dy * dy) * rx;
    int xa = (int)floorf(cx - ho - 0.5f), xb = (int)ceilf(cx + ho - 0.5f) + 1;
    if (xa < X0) xa = X0;
    if (xb > X1) xb = X1;
    int fa = 1, fb = 0;
    if (qi > 0 && fabsf(dy) < qi) {
      const float hi = sqrtf(qi * qi - dy * dy) * rx;
      fa = (int)ceilf(cx - hi - 0.5f);
      fb = (int)floorf(cx + hi - 0.5f);
    }
    for (int xx = xa; xx < xb; xx++) {
      if (xx >= fa && xx <= fb) {
        if (a >= 255) put(xx, yy, c); else put_a(xx, yy, c, a);
        continue;
      }
      const float dx = (xx + 0.5f - cx) * irx;
      const uint32_t k = cov((sqrtf(dx * dx + dy * dy) - 1.0f) * m);
      if (k) put_a(xx, yy, c, mul255(k, a));
    }
  }
}
void gfx_ring(float cx, float cy, float r, float th, uint16_t c, uint8_t a) { gfx_arc(cx, cy, r, th, 0, 360, c, a); }

// Arc core: one pass over the annulus; colour c1 on rel <= span1, then c2 on rel <= span2 (span2 < 0: none).
static void arc_core(float cx, float cy, float r, float th, float a0, float span1, uint16_t c1, float span2, uint16_t c2, uint8_t a) {
  const bool full = span1 >= 360.0f && span2 < 0;
  float s0 = fmodf(a0, 360.0f);
  if (s0 < 0) s0 += 360.0f;
  int X0, Y0, X1, Y1;
  if (!boxf(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, &X0, &Y0, &X1, &Y1)) return;
  cx += s_ox; cy += s_oy;
  const float rm = r - th * 0.5f, hw = th * 0.5f;
  const float rin = rm - hw - 1.0f, rout = r + 1.0f;
  for (int yy = Y0; yy < Y1; yy++) {
    const float dy = yy + 0.5f - cy;
    if (fabsf(dy) > rout) continue;
    // v3.1.1: only the annulus columns of this row (outer span minus the inner hole)
    const float ho = sqrtf(rout * rout - dy * dy);
    int xa = (int)floorf(cx - ho - 0.5f), xb = (int)ceilf(cx + ho - 0.5f) + 1;
    if (xa < X0) xa = X0;
    if (xb > X1) xb = X1;
    int ha = 1, hb = 0;  // hole [ha, hb]
    if (rin > 1 && fabsf(dy) < rin - 1) {
      const float hi = sqrtf((rin - 1) * (rin - 1) - dy * dy);
      ha = (int)ceilf(cx - hi - 0.5f);
      hb = (int)floorf(cx + hi - 0.5f);
    }
    for (int xx = xa; xx < xb; xx++) {
      if (xx >= ha && xx <= hb) { xx = hb; continue; }
      const float dx = xx + 0.5f - cx;
      const float d2 = dx * dx + dy * dy;
      if (d2 > rout * rout || (rin > 0 && d2 < rin * rin)) continue;
      const uint32_t k = cov(fabsf(sqrtf(d2) - rm) - hw);
      if (!k) continue;
      float rel = 0;
      if (!full) {
        float ang = gfx_atan2_deg(dy, dx);
        if (ang < 0) ang += 360.0f;
        rel = ang - s0;
        if (rel < 0) rel += 360.0f;
      }
      const uint32_t ka = mul255(k, a);
      if (full || rel <= span1) put_a(xx, yy, c1, ka);
      if (span2 >= 0 && rel <= span2) put_a(xx, yy, c2, ka);
    }
  }
}
void gfx_arc(float cx, float cy, float r, float th, float a0, float a1, uint16_t c, uint8_t a) {
  if (a1 < a0) { const float t = a0; a0 = a1; a1 = t; }
  const float span = a1 - a0;
  if (span <= 0) return;
  arc_core(cx, cy, r, th, a0, span, c, -1, 0, a);
}
void gfx_arc2(float cx, float cy, float r, float th, float a0, float span_track, uint16_t c_track, float span_fill, uint16_t c_fill) {
  if (span_track <= 0) return;
  arc_core(cx, cy, r, th, a0, span_track, c_track, span_fill > 0 ? span_fill : -1, c_fill, 255);
}

void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, uint8_t a) {
  const float hw = w * 0.5f;
  int X0, Y0, X1, Y1;
  const float bx0 = (x0 < x1 ? x0 : x1) - hw - 1, bx1 = (x0 > x1 ? x0 : x1) + hw + 1;
  const float by0 = (y0 < y1 ? y0 : y1) - hw - 1, by1 = (y0 > y1 ? y0 : y1) + hw + 1;
  if (!boxf(bx0, by0, bx1, by1, &X0, &Y0, &X1, &Y1)) return;
  x0 += s_ox; x1 += s_ox; y0 += s_oy; y1 += s_oy;
  const float vx = x1 - x0, vy = y1 - y0;
  const float L2 = vx * vx + vy * vy;
  const float inv = L2 > 1e-6f ? 1.0f / L2 : 0.0f;
  // v3.1.1: per row, only the x range inside the infinite band |cross| <= (hw + 1) * L can be covered
  const float L = sqrtf(L2), reach = (hw + 1.0f) * L;
  const bool steep = fabsf(vy) > 1e-3f;
  for (int yy = Y0; yy < Y1; yy++) {
    const float py = yy + 0.5f - y0;
    int xa = X0, xb = X1;
    if (steep && L2 > 1e-6f) {
      // px*vy - py*vx in [-reach, reach]  ->  px in [(py*vx - reach)/vy, (py*vx + reach)/vy]
      float p0 = (py * vx - reach) / vy, p1 = (py * vx + reach) / vy;
      if (p0 > p1) { const float t2 = p0; p0 = p1; p1 = t2; }
      const int a = (int)floorf(p0 + x0 - 0.5f) - 1, b = (int)ceilf(p1 + x0 - 0.5f) + 2;
      if (a > xa) xa = a;
      if (b < xb) xb = b;
    }
    for (int xx = xa; xx < xb; xx++) {
      const float px = xx + 0.5f - x0;
      float t = (px * vx + py * vy) * inv;
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      const float ex = px - vx * t, ey = py - vy * t;
      const uint32_t k = cov(sqrtf(ex * ex + ey * ey) - hw);
      if (k) put_a(xx, yy, c, mul255(k, a));
    }
  }
}
void gfx_polyline(const float *xy, int n, float w, uint16_t c) {
  for (int i = 0; i + 1 < n; i++) gfx_line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w, c);
}
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) {
  float mnx = x0, mxx = x0, mny = y0, mxy = y0;
  if (x1 < mnx) mnx = x1;
  if (x2 < mnx) mnx = x2;
  if (x1 > mxx) mxx = x1;
  if (x2 > mxx) mxx = x2;
  if (y1 < mny) mny = y1;
  if (y2 < mny) mny = y2;
  if (y1 > mxy) mxy = y1;
  if (y2 > mxy) mxy = y2;
  int X0, Y0, X1, Y1;
  if (!boxf(mnx, mny, mxx, mxy, &X0, &Y0, &X1, &Y1)) return;
  x0 += s_ox; x1 += s_ox; x2 += s_ox; y0 += s_oy; y1 += s_oy; y2 += s_oy;
  const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (fabsf(area) < 1e-3f) return;
  const float sgn = area > 0 ? 1.f : -1.f;
  for (int yy = Y0; yy < Y1; yy++) {
    const float py = yy + 0.5f;
    for (int xx = X0; xx < X1; xx++) {
      const float px = xx + 0.5f;
      const float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sgn;
      const float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sgn;
      const float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sgn;
      if (e0 >= 0 && e1 >= 0 && e2 >= 0) put(xx, yy, c);
    }
  }
}

void gfx_blit_pk(int x, int y, int w, int h, const uint16_t *pk_p, const uint8_t *pk_k) {
  int X0, Y0, X1, Y1;
  if (!pk_p || !pk_k || !box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  const int lx = x + s_ox, ly = y + s_oy;
  for (int yy = Y0; yy < Y1; yy++) {
    const uint16_t *pp = pk_p + (yy - ly) * w + (X0 - lx);
    const uint8_t *kk = pk_k + (yy - ly) * w + (X0 - lx);
    uint16_t *d = P(X0, yy);
    for (int xx = X0; xx < X1; xx++, pp++, kk++, d++) {
      const uint32_t k = *kk;
      if (k == 0) { *d = *pp; continue; }      // opaque
      if (k == 255 && *pp == 0) continue;      // untouched
      // out = P + bg * K: packed (one multiply, 5-bit K), saturating per channel
      const uint32_t k5 = (k + 4) >> 3;
      uint32_t v = spread(sw(*pp)) + (((spread(sw(*d)) * k5) >> 5) & SPREAD_M);
      const uint32_t ov = v & 0x08010020u;     // carry out of g (bit 27), r (bit 16), b (bit 5)
      if (ov) {
        if (ov & 0x08000000u) v |= 0x07E00000u;
        if (ov & 0x00010000u) v |= 0x0000F800u;
        if (ov & 0x00000020u) v |= 0x0000001Fu;
      }
      *d = sw(unspread(v & SPREAD_M));
    }
  }
}
void gfx_a4(int x, int y, int w, int h, const uint8_t *m, uint16_t c, uint8_t alpha) {
  if (!m) return;
  int X0, Y0, X1, Y1;
  if (!box(x, y, x + w, y + h, &X0, &Y0, &X1, &Y1)) return;
  const int stride = (w + 1) / 2;
  const int lx = x + s_ox, ly = y + s_oy;
  const uint32_t cs = spread(c), al = alpha;
  const uint16_t csw = sw(c);
  for (int yy = Y0; yy < Y1; yy++) {
    const uint8_t *row = m + (yy - ly) * stride;
    uint16_t *d = P(X0, yy);
    for (int xx = X0; xx < X1; xx++, d++) {
      const int i = xx - lx;
      const uint8_t b = row[i >> 1];
      const uint32_t a4 = (i & 1) ? (b & 0x0F) : (b >> 4);
      if (!a4) continue;
      const uint32_t a5 = al >= 255 ? (a4 == 15 ? 32u : (a4 * 34u) >> 4) : (a4 * 17u * al) >> 11;
      if (a5) put_a5(d, cs, csw, a5);
    }
  }
}
void gfx_a4_scaled(float cx, float cy, int w, int h, const uint8_t *m, uint16_t c, float s) {
  if (!m) return;
  if (s > 0.995f && s < 1.005f) { gfx_a4((int)(cx - w / 2), (int)(cy - h / 2), w, h, m, c); return; }
  const float hw = w * s * 0.5f, hh = h * s * 0.5f;
  int X0, Y0, X1, Y1;
  if (!boxf(cx - hw, cy - hh, cx + hw, cy + hh, &X0, &Y0, &X1, &Y1)) return;
  cx += s_ox; cy += s_oy;
  const int stride = (w + 1) / 2;
  const float inv = 1.0f / s;
  for (int yy = Y0; yy < Y1; yy++) {
    const int sy = (int)((yy + 0.5f - cy) * inv + h * 0.5f);
    if (sy < 0 || sy >= h) continue;
    for (int xx = X0; xx < X1; xx++) {
      const int sx = (int)((xx + 0.5f - cx) * inv + w * 0.5f);
      if (sx < 0 || sx >= w) continue;
      const uint8_t b = m[sy * stride + (sx >> 1)];
      const uint32_t a4 = (sx & 1) ? (b & 0x0F) : (b >> 4);
      if (a4) put_a(xx, yy, c, a4 * 17u);
    }
  }
}

// ---------------------------------------------------------------- text --
static inline const GFXglyph *glyph(const GFXfont *f, uint8_t ch) {
  if (ch < f->first || ch > f->last) return nullptr;
  return &f->glyph[ch - f->first];
}
int gfx_text_w(const GFXfont *f, const char *s, int track) {
  int w = 0, n = 0;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const GFXglyph *g = glyph(f, *p);
    if (!g) g = glyph(f, ' ');
    if (!g) continue;
    w += g->xAdvance;
    n++;
  }
  return n ? w + (n - 1) * track : 0;
}
int gfx_cap_h(const GFXfont *f) {
  const GFXglyph *g = glyph(f, '0');
  if (!g) g = glyph(f, 'H');
  return g ? -g->yOffset : f->yAdvance / 2;
}
int gfx_text(const GFXfont *f, int x, int y, const char *s, uint16_t c, int track) {
  PROF_SCOPE(PL_TEXT);
  const int vis_top = y - f->yAdvance, vis_bot = y + f->yAdvance / 2;
  const bool any = gfx_vis(vis_top, vis_bot - vis_top);
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const GFXglyph *g = glyph(f, *p);
    if (!g) g = glyph(f, ' ');
    if (!g) continue;
    if (any && g->width && g->height) {
      const int gx = x + g->xOffset, gy = y + g->yOffset;
      int X0, Y0, X1, Y1;
      if (box(gx, gy, gx + g->width, gy + g->height, &X0, &Y0, &X1, &Y1)) {
        const uint8_t *bm = f->bitmap + g->bitmapOffset;
        const int sgx = gx + s_ox, sgy = gy + s_oy;
        const uint16_t v = sw(c);
        for (int yy = Y0; yy < Y1; yy++) {
          const int row = yy - sgy;
          uint32_t bit = (uint32_t)row * g->width + (uint32_t)(X0 - sgx);
          uint16_t *dst = P(X0, yy);
          for (int xx = X0; xx < X1; xx++, bit++, dst++)
            if (bm[bit >> 3] & (0x80 >> (bit & 7))) *dst = v;
        }
      }
    }
    x += g->xAdvance + track;
  }
  return x - track;
}
int gfx_text_m(const GFXfont *f, int x, int ym, const char *s, uint16_t c, GfxAlign al, int track) {
  const int w = gfx_text_w(f, s, track);
  if (al == GA_C) x -= w / 2;
  else if (al == GA_R) x -= w;
  gfx_text(f, x, ym + gfx_cap_h(f) / 2, s, c, track);
  return x;
}
static void upcase(const char *s, char *o, int n) {
  int i = 0;
  for (; s[i] && i < n - 1; i++) o[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
  o[i] = 0;
}
int gfx_micro_w(const char *s, bool small) {
  char b[96];
  upcase(s, b, sizeof(b));
  return gfx_text_w(small ? &AF_MICRO_S : &AF_MICRO, b, 1);
}
int gfx_micro(int x, int y, const char *s, uint16_t c, GfxAlign al, bool small) {
  char b[96];
  upcase(s, b, sizeof(b));
  const GFXfont *f = small ? &AF_MICRO_S : &AF_MICRO;
  const int w = gfx_text_w(f, b, 1);
  if (al == GA_C) x -= w / 2;
  else if (al == GA_R) x -= w;
  // top-anchored like the mockup micro(): cell top at y, cap height 6-7 px
  gfx_text(f, x, y + gfx_cap_h(f) + (small ? 1 : 1), b, c, 1);
  return w;
}
void gfx_char5(int x, int y, char ch, uint16_t c) {
  if ((uint8_t)ch < FONT5X7_FIRST || (uint8_t)ch > FONT5X7_LAST) return;
  if (!gfx_vis(y, 8)) return;
  const uint8_t *g = &font5x7[((uint8_t)ch - FONT5X7_FIRST) * 5];
  for (int col = 0; col < 5; col++) {
    uint8_t bits = g[col];
    for (int row = 0; row < 7; row++, bits >>= 1)
      if (bits & 1) gfx_px(x + col, y + row, c);
  }
}
void gfx_text5(int x, int y, const char *s, int scale, uint16_t c) {
  if (!gfx_vis(y, 8 * scale)) return;
  for (; *s; s++, x += 6 * scale) {
    if (scale == 1) { gfx_char5(x, y, *s, c); continue; }
    if ((uint8_t)*s < FONT5X7_FIRST || (uint8_t)*s > FONT5X7_LAST) continue;
    const uint8_t *g = &font5x7[((uint8_t)*s - FONT5X7_FIRST) * 5];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; row++, bits >>= 1)
        if (bits & 1) gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
    }
  }
}
int gfx_text5_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

static inline const AAGlyph *aaglyph(const AAFont *f, uint8_t ch) {
  if (ch < f->first || ch > f->last) return nullptr;
  return &f->glyph[ch - f->first];
}
int gfx_aatext_w(const AAFont *f, const char *s, int track) {
  int w = 0, n = 0;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const AAGlyph *g = aaglyph(f, *p);
    if (!g) { w += f->yAdvance / 4; n++; continue; }  // space / unknown
    w += g->xAdvance;
    n++;
  }
  return n ? w + (n - 1) * track : 0;
}
int gfx_aacap_h(const AAFont *f) {
  const AAGlyph *g = aaglyph(f, '0');
  return g ? -g->yOffset : f->yAdvance / 2;
}
int gfx_aatext(const AAFont *f, int x, int y, const char *s, uint16_t c, int track, uint8_t alpha) {
  PROF_SCOPE(PL_TEXT);
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const AAGlyph *g = aaglyph(f, *p);
    if (!g) { x += f->yAdvance / 4 + track; continue; }
    if (g->width && g->height) {
      const int gx = x + g->xOffset, gy = y + g->yOffset;
      int X0, Y0, X1, Y1;
      if (box(gx, gy, gx + g->width, gy + g->height, &X0, &Y0, &X1, &Y1)) {
        const uint8_t *bm = f->bitmap + g->offset;
        const int st = (g->width + 1) >> 1, sgx = gx + s_ox, sgy = gy + s_oy;
        const uint32_t cs = spread(c), al = alpha;
        const uint16_t csw = sw(c);
        for (int yy = Y0; yy < Y1; yy++) {
          const uint8_t *row = bm + (yy - sgy) * st;
          uint16_t *d = P(X0, yy);
          for (int xx = X0; xx < X1; xx++, d++) {
            const int i = xx - sgx;
            const uint8_t b = row[i >> 1];
            const uint32_t a4 = (i & 1) ? (b & 0x0F) : (b >> 4);
            if (!a4) continue;
            // 4-bit coverage -> 5-bit alpha (15 -> opaque when alpha = 255)
            const uint32_t a5 = al >= 255 ? (a4 == 15 ? 32u : (a4 * 34u) >> 4) : (a4 * 17u * al) >> 11;
            if (a5) put_a5(d, cs, csw, a5);
          }
        }
      }
    }
    x += g->xAdvance + track;
  }
  return x - track;
}
void gfx_aatext_hero(const AAFont *f, int cx, int cym, const char *s, uint16_t fill, uint16_t stroke, uint16_t glow,
                     uint8_t glow_a, int track) {
  const int w = gfx_aatext_w(f, s, track);
  const int x = cx - w / 2, y = cym + gfx_aacap_h(f) / 2;
  if (!gfx_vis(y - gfx_aacap_h(f) - 4, gfx_aacap_h(f) + 8)) return;
  static const int8_t o2[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
  static const int8_t o1[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  if (glow_a)
    for (int i = 0; i < 4; i++) gfx_aatext(f, x + o2[i][0], y + o2[i][1], s, glow, track, glow_a);
  for (int i = 0; i < 4; i++) gfx_aatext(f, x + o1[i][0], y + o1[i][1], s, stroke, track, 255);
  gfx_aatext(f, x, y, s, fill, track, 255);
}

// ---- hero cache ----
#include <stdlib.h>
static uint32_t aa_cov_at(const AAFont *f, const char *s, int x, int y, int px, int py) {
  // coverage 0..255 of the string drawn at baseline (x, y) at pixel (px, py)
  uint32_t best = 0;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const AAGlyph *g = aaglyph(f, *p);
    if (!g) { x += f->yAdvance / 4; continue; }
    const int gx = x + g->xOffset, gy = y + g->yOffset;
    if (px >= gx && px < gx + g->width && py >= gy && py < gy + g->height) {
      const int i = px - gx, st = (g->width + 1) >> 1;
      const uint8_t b = f->bitmap[g->offset + (py - gy) * st + (i >> 1)];
      const uint32_t a = ((i & 1) ? (b & 0x0F) : (b >> 4)) * 17u;
      if (a > best) best = a;
    }
    x += g->xAdvance;
  }
  return best;
}
// Heap guard for the hero mask (v3.1.3): hal_cache_ok() refuses while a TLS job holds the
// UI-cache release and whenever the mask would eat into the TLS reserve (ui_task.cpp).
bool hal_cache_ok(uint32_t n);
static bool hero_heap_ok(size_t n) { return hal_cache_ok((uint32_t)n); }
bool gfx_hero_build(HeroMask *hm, const AAFont *f, int cx, int cym, const char *s, uint8_t glow_a) {
  if (hm->m && hm->f == f && hm->cx == cx && hm->cym == cym && hm->ga == glow_a && strncmp(hm->s, s, sizeof(hm->s)) == 0) return true;
  const int w = gfx_aatext_w(f, s, 0), cap = gfx_aacap_h(f);
  const int x = cx - w / 2, y = cym + cap / 2;
  // union of glyph boxes, grown by the 2 px glow offset
  int bx0 = 32767, by0 = 32767, bx1 = -32767, by1 = -32767, xx = x;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    const AAGlyph *g = aaglyph(f, *p);
    if (!g) { xx += f->yAdvance / 4; continue; }
    if (g->width && g->height) {
      if (xx + g->xOffset < bx0) bx0 = xx + g->xOffset;
      if (y + g->yOffset < by0) by0 = y + g->yOffset;
      if (xx + g->xOffset + g->width > bx1) bx1 = xx + g->xOffset + g->width;
      if (y + g->yOffset + g->height > by1) by1 = y + g->yOffset + g->height;
    }
    xx += g->xAdvance;
  }
  if (bx1 <= bx0) return false;
  bx0 -= 2; by0 -= 2; bx1 += 2; by1 += 2;
  const int W = bx1 - bx0, H = by1 - by0;
  if (!hm->m || hm->w * hm->h != W * H) {
    // On a failed guard the old mask (consistent with its own key fields) is kept and
    // the caller draws without a mask (gfx_aatext_hero); nothing is dereferenced.
    if (!hero_heap_ok((size_t)W * H)) return false;
    free(hm->m);
    hm->m = (uint8_t *)malloc((size_t)W * H);
    if (!hm->m) { hm->f = nullptr; return false; }  // gfx_hero_draw checks m
  }
  static const int8_t o2[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
  static const int8_t o1[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  for (int py = 0; py < H; py++)
    for (int px = 0; px < W; px++) {
      const int X = bx0 + px, Y = by0 + py;
      uint32_t tg = 255, ts = 255;  // transmission
      if (glow_a)
        for (int i = 0; i < 4; i++) {
          const uint32_t a = (aa_cov_at(f, s, x + o2[i][0], y + o2[i][1], X, Y) * glow_a + 127) / 255;
          tg = tg * (255 - a) / 255;
        }
      for (int i = 0; i < 4; i++) {
        const uint32_t a = aa_cov_at(f, s, x + o1[i][0], y + o1[i][1], X, Y);
        ts = ts * (255 - a) / 255;
      }
      const uint32_t ag = ((255 - tg) + 8) / 17, as = ((255 - ts) + 8) / 17;
      hm->m[py * W + px] = (uint8_t)((ag > 15 ? 15 : ag) << 4 | (as > 15 ? 15 : as));
    }
  hm->f = f; hm->cx = (int16_t)cx; hm->cym = (int16_t)cym; hm->ga = glow_a;
  strncpy(hm->s, s, sizeof(hm->s) - 1);
  hm->s[sizeof(hm->s) - 1] = 0;
  hm->x = (int16_t)bx0; hm->y = (int16_t)by0; hm->w = (int16_t)W; hm->h = (int16_t)H;
  return true;
}
void gfx_hero_draw(const HeroMask *hm, uint16_t fill, uint16_t stroke, uint16_t glow) {
  PROF_SCOPE(PL_TEXT);
  if (!hm || !hm->m) return;
  int X0, Y0, X1, Y1;
  if (!box(hm->x, hm->y, hm->x + hm->w, hm->y + hm->h, &X0, &Y0, &X1, &Y1)) return;
  const int lx = hm->x + s_ox, ly = hm->y + s_oy;
  for (int yy = Y0; yy < Y1; yy++) {
    const uint8_t *row = hm->m + (yy - ly) * hm->w;
    for (int xx = X0; xx < X1; xx++) {
      const uint8_t v = row[xx - lx];
      if (!v) continue;
      if (v >> 4) put_a(xx, yy, glow, (v >> 4) * 17u);
      if (v & 15) put_a(xx, yy, stroke, (v & 15) * 17u);
    }
  }
  const int w = gfx_aatext_w(hm->f, hm->s, 0), cap = gfx_aacap_h(hm->f);
  gfx_aatext(hm->f, hm->cx - w / 2, hm->cym + cap / 2, hm->s, fill, 0, 255);
}
void gfx_hero_free(HeroMask *hm) {
  if (!hm) return;
  free(hm->m);
  hm->m = nullptr;
  hm->f = nullptr;
}

int gfx_wrap(const GFXfont *f, const char *s, int w, uint16_t *starts, uint16_t *lens, int n) {
  int lines = 0;
  int i = 0;
  const int L = (int)strlen(s);
  while (i < L && lines < n) {
    while (s[i] == ' ') i++;
    if (i >= L) break;
    int last_ok = -1, x = 0, j = i;
    for (; j < L; j++) {
      if (s[j] == '\n') break;
      const GFXglyph *g = glyph(f, (uint8_t)s[j]);
      x += g ? g->xAdvance : 0;
      if (x > w) break;
      if (s[j] == ' ') last_ok = j;
    }
    int end;
    if (j >= L || s[j] == '\n') end = j;
    else if (last_ok > i) end = last_ok;
    else end = j > i ? j : i + 1;  // hard break
    starts[lines] = (uint16_t)i;
    lens[lines] = (uint16_t)(end - i);
    lines++;
    i = end + ((end < L && (s[end] == ' ' || s[end] == '\n')) ? 1 : 0);
  }
  return lines;
}

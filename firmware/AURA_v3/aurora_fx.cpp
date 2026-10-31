#include "aurora_fx.h"
#include "gfx.h"
#include "prof.h"
#include "src/assets/face_specs.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
using namespace aura::spec;

static AurField *s_f[2] = {nullptr, nullptr};
AurField *aur_field(uint8_t slot) {
  if (slot > 1) return nullptr;
  if (!s_f[slot]) {
    s_f[slot] = (AurField *)calloc(1, sizeof(AurField));
  }
  return s_f[slot];
}
void aur_field_free(void) {
  for (int i = 0; i < 2; i++) { free(s_f[i]); s_f[i] = nullptr; }
}
uint32_t aur_ram_bytes(void) { return (s_f[0] ? sizeof(AurField) : 0) + (s_f[1] ? sizeof(AurField) : 0); }

// radians -> LUT angle (1/65536 turn)
static inline uint32_t ang(float rad) {
  const float t = rad * (1.0f / 6.2831853f);
  return (uint32_t)(int32_t)lrintf((t - floorf(t)) * 65536.0f);
}
#define A_PER_RAD (65536.0f / 6.2831853f)

void aur_update(AurField *f, uint32_t now, float T, int y0, int h, int clamp_h, float boost) {
  if (!f) return;
  const bool key = f->prof_ok && f->key_boost == boost && f->key_y0 == y0 && f->key_h == h && f->key_clamp == clamp_h;
  if (key && f->valid && (uint32_t)(now - f->t_ms) < AUR_UPDATE_MS) return;  // reuse: every 2nd frame
  if (!key) {
    // vertical profile: row j of a ribbon -> k = min(j>>1, 19), f = ((len-2k)/len)^2 * peak/255 * boost
    for (int r = 0; r < 3; r++) {
      const AuroraRibbon &rb = AURORA[r];
      const float pk = rb.peak / 255.0f * boost;
      for (int j = 0; j < AUR_MAXLEN; j++) {
        int k = j >> 1;
        if (k > 19) k = 19;
        float q = (float)(rb.len - 2 * k) / rb.len;
        if (q < 0 || j >= rb.len) q = 0;
        q = q * q * pk;
        // ar = (r * q * I) >> 3 etc.; stored at I = 1 in Q8
        f->pr[r][j][0] = (uint16_t)(rb.r * q * 256.0f / 8.0f);
        f->pr[r][j][1] = (uint16_t)(rb.g * q * 256.0f / 4.0f);
        f->pr[r][j][2] = (uint16_t)(rb.b * q * 256.0f / 8.0f);
      }
    }
    f->key_boost = boost; f->key_y0 = (int16_t)y0; f->key_h = (int16_t)h; f->key_clamp = (int16_t)clamp_h;
    f->prof_ok = true;
  }
  for (int r = 0; r < 3; r++) {
    const AuroraRibbon &rb = AURORA[r];
    // phase accumulators (1/65536 turn): per 2 px column step
    uint32_t a1 = ang(1.15f * T + rb.phase), a2 = ang(-0.75f * T + rb.phase), a3 = ang(1.9f * T + 1.7f * rb.phase);
    const uint32_t d1 = (uint32_t)lrintf(0.021f * 2 * A_PER_RAD), d2 = (uint32_t)lrintf(0.052f * 2 * A_PER_RAD),
                   d3 = (uint32_t)lrintf(0.09f * 2 * A_PER_RAD);
    for (int c = 0; c < AUR_COLS; c++) {
      // y = y0 + base + 11 sin(..) + 5 sin(..)   (Q14 sines)
      const int yq = (11 * gfx_isin(a1) + 5 * gfx_isin(a2));        // Q14
      int ya = y0 + rb.base_y + (yq >> 14);                          // floor (arithmetic shift) == (int) of the positive float
      if (ya < y0 + 1) ya = y0 + 1;
      f->ya[r][c] = (int16_t)ya;
      int iv = 141 + ((115 * gfx_isin(a3)) >> 14);                   // (0.55 + 0.45 s) * 256, 0..256
      f->iq[r][c] = (uint8_t)(iv > 255 ? 255 : (iv < 0 ? 0 : iv));
      a1 += d1; a2 += d2; a3 += d3;
    }
  }
  const uint32_t a0 = ang(2 * T), di = (uint32_t)lrintf(A_PER_RAD);
  for (int i = 0; i < AUR_STARS; i++) {
    const int tw = gfx_isin(a0 + di * (uint32_t)i);  // sin(2T + i), Q14
    f->star_c[i] = tw > 9011 ? 0 : (tw > -3277 ? 1 : 2);  // 0.55 / -0.2
  }
  f->t_ms = now;
  f->valid = true;
}

void aur_draw(const AurField *f, int y0, int h, uint16_t c_txt, uint16_t c_dim, uint16_t bg) {
  PROF_SCOPE(PL_AURORA);
  if (!f || !f->valid || !gfx_vis(y0, h)) return;
  gfx_clip(0, y0, GFX_W, y0 + h);
  const uint16_t faint = blend565(bg, c_dim, 120);
  for (int i = 0; i < AUR_STARS; i++) {
    const uint32_t s = (uint32_t)(i + 7) * 2654435761u;
    const int sx = (int)(s % 320), sy = y0 + (int)((s >> 9) % (uint32_t)(h - 4));
    if (!gfx_vis(sy, 1)) continue;
    gfx_px(sx, sy, f->star_c[i] == 0 ? c_txt : (f->star_c[i] == 1 ? c_dim : faint));
  }
  const int b0 = gfx_y0() > y0 ? gfx_y0() : y0, b1 = gfx_y1() < y0 + h ? gfx_y1() : y0 + h;
  const int yclamp = f->key_clamp > 0 ? y0 + f->key_clamp : 32767;
  // column-major over the band rows; direct buffer access (row pointers fetched once per band)
  uint16_t *rows[64];
  int lx0 = 0, lx1 = 0, ox = 0;
  const int nb = b1 - b0;
  if (nb <= 0 || nb > 64) { gfx_noclip(); return; }
  for (int y = b0; y < b1; y++) rows[y - b0] = gfx_rowp(y, &lx0, &lx1, &ox);
  for (int r = 0; r < 3; r++) {
    const int len = AURORA[r].len;
    const int16_t *yar = f->ya[r];
    const uint8_t *iqr = f->iq[r];
    for (int c = 0; c < AUR_COLS; c++) {
      const int ya = yar[c];
      int ye = ya + len;
      if (ye > yclamp) ye = yclamp;
      if (ye <= b0 || ya >= b1) continue;
      const int x = c * 2;
      if (x + 1 < lx0 || x >= lx1) continue;
      const bool two = x + 1 < lx1, one = x >= lx0;
      const uint32_t I = iqr[c] + (iqr[c] >> 7);  // 0..256
      const int fg = (int)((30 * I) >> 10), fbb = (int)((16 * I) >> 11);
      const bool fringe_ok = f->key_clamp <= 0 || ye - 2 > ya;
      const int y1 = ye < b1 ? ye : b1;
      for (int y = ya > b0 ? ya : b0; y < y1; y++) {
        uint16_t *row = rows[y - b0];
        if (!row) continue;
        const uint16_t *p = f->pr[r][y - ya];
        int ar = (int)((p[0] * I) >> 16), ag = (int)((p[1] * I) >> 16), ab = (int)((p[2] * I) >> 16);
        if (fringe_ok && y >= ye - 2) { ag += fg; ab += fbb; }
        if (!(ar | ag | ab)) continue;
        uint16_t *d = &row[x + ox];
        for (int k = 0; k < 2; k++, d++) {
          if (k == 0 ? !one : !two) continue;
          const uint16_t v = (uint16_t)((*d >> 8) | (*d << 8));
          int rr = ((v >> 11) & 31) + ar, gg = ((v >> 5) & 63) + ag, bb = (v & 31) + ab;
          if (rr > 31) rr = 31;
          if (gg > 63) gg = 63;
          if (bb > 31) bb = 31;
          const uint16_t o = (uint16_t)((rr << 11) | (gg << 5) | bb);
          *d = (uint16_t)((o >> 8) | (o << 8));
        }
      }
    }
  }
  gfx_noclip();
}

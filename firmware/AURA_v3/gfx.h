// gfx.h - band renderer for AURA v3.1. Pure C++ (no Arduino), also built by the host shim.
//
// The panel is drawn as horizontal bands (UI_BAND_H rows). A screen's draw()
// runs once per band; every primitive clips to the current band (and to an
// optional clip rect), so drawing the full scene each band is cheap: shapes
// outside the band return after one compare.
//
// Colours are plain RGB565 (0xRRRRRGGGGGGBBBBB). The band buffer holds
// byte-swapped (big-endian) RGB565 for the ILI9341; the renderer swaps on
// write and on read-for-blend. A4 masks (4-bit alpha, high nibble first,
// stride ceil(w/2)) are blended in by the renderer itself.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "aura_fonts.h"

#define GFX_W 320
#define GFX_H 240

// ---------------------------------------------------------------- colour --
static inline uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// t = 0..255 from a toward b (AURA v2 blend565 semantics).
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t);
// Scale a colour toward black: k = 0..256.
uint16_t dim565(uint16_t c, uint16_t k);

// ------------------------------------------------------------------ band --
void gfx_begin(uint16_t *buf, int y0, int h);
// Render into an arbitrary w-wide buffer (cache builds). Restore with gfx_begin() before band drawing.
void gfx_begin_ex(uint16_t *buf, int y0, int h, int w);
// v3.1.3: compose only the window x0..x0+w-1 of rows y0..y0+h-1 (buffer stride w), screen coords unchanged.
void gfx_begin_win(uint16_t *buf, int x0, int y0, int w, int h);
int gfx_y0(void);
int gfx_y1(void);                                  // exclusive
static inline bool gfx_vis(int y, int h);          // rows [y, y+h) touch the band?
void gfx_clip(int x0, int y0, int x1, int y1);     // exclusive x1/y1, intersected with the screen
void gfx_noclip(void);
// Translate all subsequent drawing by (dx, dy) (page slides, bobbing faces).
void gfx_origin(int dx, int dy);
int gfx_ox(void);
int gfx_oy(void);

// ------------------------------------------------------------ primitives --
void gfx_fill(uint16_t c);
void gfx_px(int x, int y, uint16_t c);
void gfx_px_a(int x, int y, uint16_t c, uint8_t a);   // alpha 0..255
void gfx_px_add(int x, int y, int r5, int g6, int b5);// additive light (saturating)
uint16_t gfx_get(int x, int y);                       // current pixel (RGB565) or 0
// Raw row access for hot loops: row y (local) inside the clip -> base pointer (index [lx + ox],
// byte-swapped pixels) and the visible local x range [lx0, lx1). nullptr if the row is clipped.
uint16_t *gfx_rowp(int y, int *lx0, int *lx1, int *ox);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rect_a(int x, int y, int w, int h, uint16_t c, uint8_t a);
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
void gfx_frame(int x, int y, int w, int h, uint16_t c);
void gfx_vgrad(int x, int y, int w, int h, uint16_t top, uint16_t bot);
// Anti-aliased shapes (signed-distance coverage, 1 px soft edge).
void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c, uint8_t a = 255);
void gfx_rrect_frame(int x, int y, int w, int h, int r, uint16_t c, uint8_t a = 255);
// Two-tone matte tile (split-flap): top half `top`, bottom half `bot`, 1 px rim.
void gfx_tile(int x, int y, int w, int h, int r, uint16_t top, uint16_t bot, uint16_t rim);
void gfx_circle(float cx, float cy, float r, uint16_t c, uint8_t a = 255);
void gfx_ring(float cx, float cy, float r, float th, uint16_t c, uint8_t a = 255);
void gfx_ellipse(float cx, float cy, float rx, float ry, uint16_t c, uint8_t a = 255);
// Arc band of radius r (outer edge) and thickness th, from a0 to a1 degrees,
// clockwise from 3 o'clock (Pillow convention). Flat ends.
void gfx_arc(float cx, float cy, float r, float th, float a0, float a1, uint16_t c, uint8_t a = 255);
// Gauge arc: track (a0 .. a0+span_track) and fill (a0 .. a0+span_fill) on top, in one pass.
void gfx_arc2(float cx, float cy, float r, float th, float a0, float span_track, uint16_t c_track, float span_fill, uint16_t c_fill);
// Capsule line (round caps), width w (>= 1). AA.
void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, uint8_t a = 255);
void gfx_polyline(const float *xy, int n, float w, uint16_t c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c);
// A4 mask, tinted. alpha scales the mask (255 = as is).
void gfx_a4(int x, int y, int w, int h, const uint8_t *mask, uint16_t c, uint8_t alpha = 255);
// Same, scaled by s (1.0 = native), centred on (cx, cy); nearest-neighbour.
void gfx_a4_scaled(float cx, float cy, int w, int h, const uint8_t *mask, uint16_t c, float s);

// Premultiplied cache blit: out = P + bg * K/256 per channel (P byte-swapped like the band, w*h each).
// K = 0 opaque (copy), K = 255 with P = 0 leaves the pixel alone.
void gfx_blit_pk(int x, int y, int w, int h, const uint16_t *pk_p, const uint8_t *pk_k);
// Fast math (no libm in pixel loops): degrees atan2 (<0.1 deg error), LUT sine.
float gfx_atan2_deg(float y, float x);
int gfx_isin(uint32_t a16);     // a16: 65536 = full turn; returns sin * 16384
float gfx_fsin(float rad);      // LUT sine (|err| < 1e-4)
static inline float gfx_fcos(float rad) { return gfx_fsin(rad + 1.5707963f); }

// ------------------------------------------------------------------ text --
// GFXfont 1 bpp. y is the BASELINE. track = extra px between glyphs.
// Strings are ASCII plus 0x7F deg, 0x80 middle dot, 0x81 ellipsis, 0x82 chevron.
int gfx_text(const GFXfont *f, int x, int y, const char *s, uint16_t c, int track = 0);
int gfx_text_w(const GFXfont *f, const char *s, int track = 0);
int gfx_cap_h(const GFXfont *f);                  // height of '0' (or 'H') above the baseline
enum GfxAlign : uint8_t { GA_L = 0, GA_C, GA_R };
// Vertically centred on ym (cap height), horizontally per align. Returns the left x used.
int gfx_text_m(const GFXfont *f, int x, int ym, const char *s, uint16_t c, GfxAlign al = GA_L, int track = 0);
// Micro caps: AF_MICRO / AF_MICRO_S, uppercased, +1 px tracking (font table rule). top-anchored.
int gfx_micro(int x, int y, const char *s, uint16_t c, GfxAlign al = GA_L, bool small = false);
int gfx_micro_w(const char *s, bool small = false);
// 5x7 bitmap font, 6x8 cell * scale (diagnostics, matrix rain).
void gfx_text5(int x, int y, const char *s, int scale, uint16_t c);
void gfx_char5(int x, int y, char ch, uint16_t c);
int gfx_text5_w(const char *s, int scale);
// A4 anti-aliased hero digits (fonts_poppins_aa.h). Same anchoring as gfx_text.
int gfx_aatext(const AAFont *f, int x, int y, const char *s, uint16_t c, int track = 0, uint8_t alpha = 255);
int gfx_aatext_w(const AAFont *f, const char *s, int track = 0);
int gfx_aacap_h(const AAFont *f);
// Centred on (cx, cym) with an optional cheap baked glow (4 offsets at 2 px) and a 1 px dark stroke.
void gfx_aatext_hero(const AAFont *f, int cx, int cym, const char *s, uint16_t fill, uint16_t stroke,
                     uint16_t glow, uint8_t glow_a, int track = 0);
// v3.1.1 hero cache: the 4 glow passes and the 4 stroke passes of gfx_aatext_hero() are
// pre-composited into one mask (high nibble = combined glow alpha 1-prod(1-a_i), low nibble =
// combined stroke alpha), 1 byte per pixel; per frame: one cached pass + the fill pass.
struct HeroMask {
  const AAFont *f;
  char s[8];
  int16_t cx, cym, x, y, w, h;  // key + mask box (local coords)
  uint8_t ga;
  uint8_t *m;
};
bool gfx_hero_build(HeroMask *hm, const AAFont *f, int cx, int cym, const char *s, uint8_t glow_a);  // malloc; false = no RAM
void gfx_hero_draw(const HeroMask *hm, uint16_t fill, uint16_t stroke, uint16_t glow);
void gfx_hero_free(HeroMask *hm);
static inline uint32_t gfx_hero_bytes(const HeroMask *hm) { return hm && hm->m ? (uint32_t)hm->w * hm->h : 0; }
// Word-wrap s into lines no wider than w px. Writes line start offsets into
// starts[] (max n) and returns the line count. Does not modify s.
int gfx_wrap(const GFXfont *f, const char *s, int w, uint16_t *starts, uint16_t *lens, int n);

// --------------------------------------------------------------- inlines --
extern int g_gfx_y0, g_gfx_y1;  // band rows in LOCAL coords (origin applied)
static inline bool gfx_vis(int y, int h) { return y < g_gfx_y1 && y + h > g_gfx_y0; }

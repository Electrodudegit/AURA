// aurora_fx.h - aurora as a cached low-res field (v3.1.2: boot animation only;
// slot 0 is allocated on first use in boot and freed in boot_leave).
// The ribbon curves (one sample per 2 px column, 160 columns x 3 ribbons), the
// per-column intensity, the 34 star twinkles and the per-row vertical profile
// are computed ONCE per update (LUT sine, fixed point) instead of per band and
// per pixel with sinf/float. The field updates at most every AUR_UPDATE_MS
// (every 2nd frame at 25-30 fps) and each band just adds the precomputed light.
// Same look as the v3.1.0 per-pixel version (face_specs.h AURORA, +-1 LSB).
#pragma once
#include <stdint.h>
#define AUR_COLS 160
#define AUR_MAXLEN 40
#define AUR_STARS 34
#define AUR_UPDATE_MS 60
struct AurField {
  uint32_t t_ms;       // g_ui.now of the last update
  float key_boost;     // profile key
  int16_t key_y0, key_h, key_clamp;
  bool valid, prof_ok;
  int16_t ya[3][AUR_COLS];      // first row of each ribbon column (screen-local y)
  uint8_t iq[3][AUR_COLS];      // intensity 0..255 (0.55 + 0.45 sin)
  uint16_t pr[3][AUR_MAXLEN][3];// per row offset: r5/g6/b5 add at full intensity, Q8
  uint8_t star_c[AUR_STARS];    // 0 bright, 1 dim, 2 faint
};
// y0/h: ribbon area (clip rows). clamp_h > 0: ribbons end at y0 + clamp_h (boot). boost: AuroraUse.boost.
// T: aurora time (already scaled). Allocated on first use (heap, ~2.3 KB); NULL if out of memory.
AurField *aur_field(uint8_t slot);   // slot 0: home F1/boot, 1: home F2
void aur_field_free(void);
void aur_update(AurField *f, uint32_t now, float T, int y0, int h, int clamp_h, float boost);
void aur_draw(const AurField *f, int y0, int h, uint16_t c_txt, uint16_t c_dim, uint16_t bg);
uint32_t aur_ram_bytes(void);


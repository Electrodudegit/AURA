# AURA v3.1.0 firmware display assets

These are for an ESP32-WROOM driving an ILI9341 at 320x240 landscape in RGB565, with the custom ESP-IDF `spi_master` band renderer.

- **All headers depend only on `<stdint.h>`.** None of them uses TFT_eSPI, Lovyan or Adafruit_GFX.
- `PROGMEM` is guarded with `#ifndef PROGMEM / #define PROGMEM / #endif`, so on ESP32 everything lands in flash `.rodata`.
- `GFXfont` and `GFXglyph` are defined in `fonts/gfxfont_types.h` behind `#ifndef _GFXFONT_H_`, using the Adafruit layout.
- Colours are **plain RGB565**: no byte swap applied. `disp_c()` does the swap.

## Developer quick start
1. **Create one asset translation unit, e.g. `ui_assets.cpp`, and include the data headers there only.** Every array is `static const`, so including a header in two `.cpp` files puts two copies in Flash.
   ```cpp
   // ui_assets.cpp - the ONLY file that includes the data headers
   #include "fw_assets/fonts/fonts_poppins.h"     // 1-bit GFXfonts: F_P_SB12 ... / AF_* roles
   #include "fw_assets/fonts/fonts_poppins_aa.h"  // optional A4 hero digits: F_PAA_B74D ...
   #include "fw_assets/icons/icons_a4.h"          // ICONS_16/24/48[ICON_COUNT]
   #include "fw_assets/icons/icons_anim_a4.h"     // optional living icons (includes icons_a4.h)
   #include "fw_assets/icons/glyphs_ui_a4.h"      // GLYPHS_KEY_24[KEY_GLYPH_*] macro keys, GLYPHS_STATE_48 (key/monitor/phone)
   #include "fw_assets/icons/glyphs_wx_a4.h"      // wx_glyph(wx_from_wmo(code, is_day), 24|32|48) -> layered weather glyph
   const GFXfont *font_sb12(void) { return &F_P_SB12; }  // ...expose what ui_task needs
   const uint8_t *icon48(int i)   { return ICONS_48[i]; }
   ```
2. **Small headers are safe anywhere.**
   - `theme_v3.h` is `#define`s plus one 52-byte theme table.
   - `face_specs.h` is constexpr only: no storage unless a value is odr-used. Its `PARTICLES[40]` array (320 B) should be used from one `.cpp`.
   - `theme_v2.h` = `theme_v3.h` plus the C++ layout tokens in `namespace aura`.
3. **Colour name conflicts.** `ui_task.cpp` currently `#define`s its own `C_BG` and `C_ACCENT`. Remove those before including `theme_v3.h` / `theme_v2.h`, otherwise GCC warns about the redefinition and the last definition wins.
4. **Drawing.**
   - **1-bit text:** walk `width*height` bits MSB-first from `bitmap[bitmapOffset]`. `x0 = cursor + xOffset`, `y0 = baseline + yOffset`.
   - **A4 icons and AA digits:** stride `(w+1)/2`, high nibble = left pixel. Alpha 15 is a plain store; 1–14 use `blend565()` (snippet in `icons_a4.h`); 0 is skipped.
   - **Clipping:** clip to your band (`y` within `[band_y, band_y+UI_BAND_H)`).
5. **Degree sign** is code **0x7F** in all text fonts: `"27" F_P_DEG_STR "C"` (same as `AF_DEG`). Convert UTF-8 `°` (C2 B0) to 0x7F before drawing.
6. **Linking.** Unused fonts and icons are dropped by `-Os` plus `--gc-sections` as long as nothing references them.

## Index
| File | What it holds |
|---|---|
| `theme_v3.h` | **Your format.** Plain RGB565 `#define`s, C/C++ compatible. See the section below |
| `theme_v2.h` | C++ layer. Includes `theme_v3.h` and adds layout tokens in `namespace aura` (`R_TILE`, `STATUS_H`, `GRID_X`, …). Keeps the old v2 colour names as aliases (`C_TEXT`→`C_TXT`, `C_ACCENT`→`C_ACC`, `C_H_*`→`C_HOME_*`); no colour is defined twice |
| `fonts/fonts_poppins.h` | Main font header. Includes all 15 fonts, with `F_P_<R/SB/B><px>[N/D]` short names and `AF_*` role names (same objects). Also the extended glyph defines (0x7F °, 0x80 ·, 0x81 …, 0x82 ›), the DejaVu→Poppins mapping and the per-screen element→font table |
| `fonts/fonts.h` | Just `#include "fonts_poppins.h"` |
| `fonts/gfxfont_types.h` | GFXglyph / GFXfont structs and the PROGMEM guard |
| `fonts/poppins_<weight><px>[_num/_digits].h` | One `static const GFXfont` each |
| `fonts/fonts_poppins_aa.h` | **Optional.** 4-bit anti-aliased Bold 34 (`-./0-9:`) and 46/58/74 (`0-9:`). `AAFont`/`AAGlyph` structs documented in the header, same A4 packing as the icons. Names `F_PAA_B34N/B46D/B58D/B74D` |
| `fonts/OFL.txt`, `OFL.txt` | SIL Open Font License 1.1 for Poppins |
| `icons/icons_a4.h` | 17 launcher icons as A4 masks at 16, 24 and 48 px: the 15 v2 apps plus **MACRO** (keyboard) and **SETTINGS** (sliders) at index 15/16 (`ICON_IDX_*` enum, `ICON_COUNT 17`). Packing doc and reference blend |
| `icons/glyphs_ui_a4.h` | Macro key glyphs at 24 px: `GLYPH_KEY_{PREV,PLAYPAUSE,NEXT,VOLDN,MUTE,VOLUP,BT}_24`, table `GLYPHS_KEY_24[KEY_GLYPH_COUNT]`. Optional 48 px state glyphs: `GLYPH_{KEY,MONITOR,PHONE_WIFI}_48`, table `GLYPHS_STATE_48` |
| `icons/glyphs_wx_a4.h` | 12 weather glyphs at 24 and 32 px, plus 48 px for CLEAR_DAY and PARTLY_DAY. **One A4 mask per colour layer** (≤2 layers, back→front, pre-occluded) tinted with `C_WX_*` via `WX_TINT_RGB565[]`. Also `WxGlyph`/`WxLayer` structs, the `wx_glyph_t` enum, `wx_from_wmo(code, is_day)` and `wx_glyph(id, size)` |
| `icons/icons_anim_a4.h` | Living-icon frames F1–F7 (13 icons, 24 and 48 px), procedural HOME/FOCUS bases, `ICON_SEQ`, `ICON_LOOP_MS`, `ICON_ANIM_STEP_MS`. Tables are sized `ICON_COUNT` (17); MACRO and SETTINGS have `ICON_SEQ_LEN 0` / `ICON_SEQ 0`, so draw them static |
| `face_specs.h` | `namespace aura::spec`: constexpr geometry, timing and parameters for every face, the menu and the status bar |
| `SPEC.md` | Readable spec, dirty regions and the uncertain values |
| `verify_fonts_icons.png` | Decode check rendered from the header text: all fonts, icons, animation frames, and AA vs 1-bit digits |
| `_src/` | Generators (`make_fonts.py`, `make_aa_fonts.py`, `make_theme_v3.py`, `make_icons.py`, `extra_glyphs.py`, `make_glyphs.py`, `gfxfont_conv.py`, `verify.py`) and the TTFs. Not for the build |

## theme_v3.h
- **Core:** `C_BG`, `C_TXT`, `C_MUT`, `C_DIM`, `C_ACC`, `C_UP`, `C_DOWN`, `C_WARN`.
- **Tile / line / focus:** `C_TILE`, `C_TILE_TOP`, `C_TILE_HI`, `C_TILE_HI_TOP`, `C_TILE_RIM`, `C_LINE`, `C_FOCUS_BORDER`, `C_SHADOW`, `C_ICON`, `C_DOT_IDLE`, `C_LAST*`, `C_EXAMPLE_TAG`, `C_TEAM2`.
- **AQI:** `C_AQI_GOOD … C_AQI_HAZARDOUS` with `AQI_BAND_COLORS[6]` and `AQI_BREAKS[7]`.
- **Home:** `C_HOME_*` (graphite, cyan, aurora, flap, rain, F6) and `C_HOME_AQI_*`.
- **Weather glyph tints (muted):** `C_WX_SUN`, `C_WX_MOON`, `C_WX_CLOUD`, `C_WX_CLOUD_DARK`, `C_WX_RAIN`, `C_WX_SNOW`, `C_WX_BOLT`, `C_WX_FOG`.
- **Per-app accents:** `C_ACC_<APP>` for all 15 apps (HOME … BUDDY), each equal to `C_ACC`.
- **Theme table:** `aura_theme_t AURA_THEMES[THEME_COUNT]` with one entry, `"Dark"` (`THEME_DARK 0`, `THEME_COUNT 1`).

## Flash cost
All figures are `.rodata` measured with xtensa-esp32-elf-g++ `-Os`.

| Group | Flash |
|---|---|
| Fonts, 1-bit, 15 headers | 27,349 B (computed 27,201; 8/9 px degree ring hand-tuned, same size) |
| AA hero digits (optional) | 25,432 B (computed 25,410) |
| Icons static 16 + 24 + 48 px, 17 icons (incl. pointer tables) | 26,872 B (was 23,712; MACRO + SETTINGS add 3,136 B of masks) |
| UI glyphs: macro keys 24 px | 2,048 B |
| UI glyphs: state 48 px (optional) | 3,472 B (`glyphs_ui_a4.h` together: 5,520 B) |
| Weather glyphs 24 + 32 + 48 px (layered) | 21,008 B (masks: 24 px 6,048, 32 px 10,752, 48 px 3,456) |
| Anim 24 px, linked alone | 31,004 B |
| Anim 48 px, linked alone | 122,588 B |
| Anim SEQ / loop tables | 387 B |
| theme_v3 tables (theme struct + AQI arrays) | 110 B |
| **Everything in one TU (measured)** | **241,598 B ≈ 236 KB** (budget ~260 KB; was 211,894 B, delta +29,704 B) |

The "linked alone" figures include frame-0 masks that are shared with the static icons; that overlap is why the parts add up to more than the measured total.

- **Leaner options:**
  - No animation: ≈ 104 KB.
  - Animation at 48 px only (grid), skip the 24 px set: ≈ 214 KB.
  - Drop the optional state glyphs (−3.5 KB) or the 48 px weather glyphs (−3.5 KB).

### 1-bit fonts
| Font | Short name | Weight | px | Range (glyphs) | yAdvance | Flash B |
|---|---|---|---|---|---|---|
| `poppins_semibold8` | `F_P_SB8` | SemiBold | 8 | 0x20–0x82 (99) | 12 | 1,129 |
| `poppins_semibold9` | `F_P_SB9` | SemiBold | 9 | 0x20–0x82 (99) | 14 | 1,153 |
| `poppins_semibold12` | `F_P_SB12` | SemiBold | 12 | 0x20–0x82 (99) | 18 | 1,442 |
| `poppins_semibold13` | `F_P_SB13` | SemiBold | 13 | 0x20–0x82 (99) | 20 | 1,494 |
| `poppins_semibold14` | `F_P_SB14` | SemiBold | 14 | 0x20–0x82 (99) | 21 | 1,618 |
| `poppins_semibold16` | `F_P_SB16` | SemiBold | 16 | 0x20–0x82 (99) | 24 | 1,843 |
| `poppins_semibold18` | `F_P_SB18` | SemiBold | 18 | 0x20–0x82 (99) | 27 | 2,125 |
| `poppins_regular10` | `F_P_R10` | Regular | 10 | 0x20–0x82 (99) | 15 | 1,233 |
| `poppins_regular12` | `F_P_R12` | Regular | 12 | 0x20–0x82 (99) | 18 | 1,385 |
| `poppins_bold22` | `F_P_B22` | Bold | 22 | 0x20–0x82 (99) | 33 | 2,953 |
| `poppins_bold26` | `F_P_B26` | Bold | 26 | 0x20–0x82 (99) | 39 | 3,787 |
| `poppins_bold34_num` | `F_P_B34N` | Bold | 34 | 0x20–0x3A (27) | 51 | 1,421 |
| `poppins_bold46_digits` | `F_P_B46D` | Bold | 46 | 0x30–0x3A (11) | 69 | 1,128 |
| `poppins_bold58_digits` | `F_P_B58D` | Bold | 58 | 0x30–0x3A (11) | 87 | 1,727 |
| `poppins_bold74_digits` | `F_P_B74D` | Bold | 74 | 0x30–0x3A (11) | 111 | 2,763 |
| **Total** | | | | | | **27,201** |

### AA (A4) hero digits, Poppins Bold
| Font | Short name | px | Range (glyphs) | Flash B |
|---|---|---|---|---|
| `poppins_aa_bold34_num` | `F_PAA_B34N` | 34 | 0x2D–0x3A (14) | 2,900 |
| `poppins_aa_bold46_digits` | `F_PAA_B46D` | 46 | 0x30–0x3A (11) | 4,482 |
| `poppins_aa_bold58_digits` | `F_PAA_B58D` | 58 | 0x30–0x3A (11) | 6,942 |
| `poppins_aa_bold74_digits` | `F_PAA_B74D` | 74 | 0x30–0x3A (11) | 11,086 |
| **Total** | | | | **25,410** |

**Font notes:**
- Medium weight is not used: DejaVu Bold ≤18 px maps to SemiBold and ≥22 px to Bold.
- AA coverage is FreeType light-hinted 8-bit, quantised to 4 bits. Advances can differ by about 1 px from the 1-bit mono-hinted fonts, so measure strings with the same font you draw with.

## Verification
- `xtensa-esp32-elf-g++ -std=gnu++17 -Wall -Wextra -fsyntax-only`: all 26 headers on their own and all together, **0 errors and 0 warnings**.
- All headers together also pass with gnu++11.
- `theme_v3.h`, `fonts_poppins_aa.h`, `glyphs_ui_a4.h` and `glyphs_wx_a4.h` also pass as C99 with `xtensa-esp32-elf-gcc`.
- Static asserts confirm `THEME_COUNT == 1`, `C_ACC_BUDDY == C_ACC`, `ICON_COUNT == 17`, `ICON_IDX_SETTINGS == 16`, `WX_GLYPH_COUNT == 12`, `KEY_GLYPH_COUNT == 7`, `STATE_GLYPH_COUNT == 3`.
- `_src/verify.py` parses the generated header text, decodes every GFXfont, the A4 icons and frames, and the AA digit fonts, then renders `verify_fonts_icons.png`. AA `12:07` sits next to the 1-bit version on the home background. Also decoded: the 17 icons, the key and state glyphs, all weather glyphs composited from their tinted layers, and an x4 zoom of the 8/9 px degree ring.

## License
- **Poppins:** © 2020 The Poppins Project Authors, SIL Open Font License 1.1 (`fonts/OFL.txt`).
  - Converted bitmaps are a Modified Version. Firmware embedding is allowed; keep `OFL.txt` with any redistributed source, and do not sell the font by itself.
- **Icons, theme and specs:** original AURA work. All mockup data is example data.

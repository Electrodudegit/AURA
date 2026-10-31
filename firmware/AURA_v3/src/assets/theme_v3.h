// theme_v3.h - AURA v3.1.0 theme, plain RGB565 #defines (NOT byte-swapped; the display driver swaps).
// C and C++ compatible, depends only on <stdint.h>. theme_v2.h includes this file and adds C++ layout tokens.
// NOTE: these are object-like macros. If a .cpp already #defines C_BG / C_ACC etc. itself, remove those local
// defines before including this header (GCC warns on redefinition with a different value).
#pragma once
#include <stdint.h>

// ---- core (v2 matte palette: menu, apps, settings) ----
#define C_BG               0x0884u  // (13, 19, 36) background (flat; gradients band in RGB565)
#define C_TXT              0xF7BFu  // (244, 247, 252) primary text, focused icon
#define C_MUT              0x9D58u  // (156, 170, 198) secondary text
#define C_DIM              0x63B2u  // (102, 118, 150) hints, labels
#define C_ACC              0x657Cu  // (96, 172, 226) single accent (sky blue)
#define C_UP               0x6E72u  // (110, 206, 150) positive
#define C_DOWN             0xEBCFu  // (232, 122, 122) negative / live dot
#define C_WARN             0xEE2Du  // (232, 196, 104) warning / stale / EXAMPLE tag base
// ---- tile / line / focus / chips / derived ----
#define C_BG_DEEP          0x0884u  // (13, 19, 36) == BG (flat)
#define C_TILE             0x1928u  // (29, 39, 64) card / tile bottom half
#define C_BUDDY_EYE        0x56FFu  // v3.1.4 Buddy v2 eye (spec §1)
#define C_BUDDY_HEART      0xFAD2u  // v3.1.4 Buddy v2 love hearts
#define C_TILE_TOP         0x2169u  // (34, 45, 72) tile top half (split-flap two-tone)
#define C_TILE_HI          0x29ECu  // (47, 62, 98) focused / lifted tile
#define C_LINE             0x320Cu  // (52, 66, 100) hairlines, dividers, status underline
#define C_ICON             0xC69Cu  // (196, 208, 228) idle icon tint
#define C_LAST             0xE58Du  // (226, 176, 104) LAST chip amber
#define C_TEAM2            0x9537u  // (150, 164, 190) neutral second-team badge
#define C_SHADOW           0x0042u  // (6, 10, 19) focus drop shadow
#define C_FOCUS_BORDER     0x4393u  // (70, 113, 158) focus border = TILE_HI->ACCENT 47%
#define C_LAST_FILL        0x41E8u  // (64, 63, 71) LAST chip fill
#define C_LAST_OUTLINE     0x730Au  // (113, 98, 81) LAST chip outline
#define C_EXAMPLE_TAG      0x83A9u  // (133, 116, 73) EXAMPLE tag text
#define C_DOT_IDLE         0x4AADu  // (74, 84, 105) inactive page dot
#define C_TILE_HI_TOP      0x3A4Du  // (57, 72, 106) focused tile top half
#define C_TILE_RIM         0x31CAu  // (48, 59, 84) tile top rim line
#define C_ICON_IDLE_LIST   0xC69Cu  // (196, 208, 228) list idle icon

// ---- AQI bands (US AQI 0-50, 51-100, 101-150, 151-200, 201-300, 301-500) ----
#define C_AQI_GOOD            0x6E72u
#define C_AQI_MODERATE        0xEE2Du
#define C_AQI_USG             0xDCCCu
#define C_AQI_UNHEALTHY       0xEBCFu
#define C_AQI_VERY_UNHEALTHY  0xAC39u
#define C_AQI_HAZARDOUS       0xA2ADu

// ---- HOME faces: graphite + cyan + aurora (v1 look kept for F1/D2, F2, F5, F6) ----
#define C_HOME_BG              0x0862u  // (10, 13, 20) graphite background
#define C_HOME_SURF            0x1906u  // (27, 35, 50) surface / F6 time chip
#define C_HOME_SURF2           0x29A9u  // (42, 54, 74) gauge track
#define C_HOME_LINE            0x3A6Cu  // (62, 78, 102)
#define C_HOME_TEXT            0xFFFFu  // (255, 255, 255)
#define C_HOME_MUTED           0xB5FAu  // (176, 190, 208)
#define C_HOME_DIM             0x7C54u  // (120, 136, 160)
#define C_HOME_UP              0x4752u  // (64, 232, 150) battery fill
#define C_HOME_DOWN            0xFB4Du  // (255, 106, 110)
#define C_HOME_WARN            0xFE69u  // (255, 206, 72) AQI gauge / EXAMPLE base
#define C_HOME_ACCENT          0x471Fu  // (64, 224, 255) HOME cyan (colon, dots, date dot)
#define C_HOME_TEMP            0xFD88u  // (255, 178, 66) TEMP gauge
#define C_HOME_HUMID           0x5E3Fu  // (92, 196, 255) HUMID gauge / F5 accent
#define C_HOME_FLAP_TOP        0x2967u  // (40, 46, 58) split-flap tile top
#define C_HOME_FLAP_BOT        0x18E5u  // (27, 31, 40) split-flap tile bottom
#define C_HOME_FLAP_GLOSS      0x428Cu  // (70, 80, 98) tile top gradient end
#define C_HOME_FLAP_HINGE      0x0041u  // (6, 8, 12) hinge line
#define C_HOME_FLAP_HINGE_LT   0x426Bu  // (70, 78, 92) line under hinge
#define C_HOME_PIN             0x9D36u  // (158, 166, 180) hinge pin
#define C_HOME_PIN_LT          0xD6DCu  // (210, 216, 226) pin highlight
#define C_HOME_BOTTOM          0x0041u  // (7, 9, 15) lower half gradient end (y150->240)
#define C_HOME_AUR0            0x2E7Fu  // (40, 205, 255) aurora ribbon cyan
#define C_HOME_AUR1            0xBC1Fu  // (190, 130, 255) aurora ribbon violet
#define C_HOME_AUR2            0x3F75u  // (60, 238, 170) aurora ribbon green
#define C_HOME_DIGIT_SHADOW    0x0041u  // (6, 8, 14) 2 px stroke around glow digits
#define C_HOME_RAIN            0x85DEu  // (134, 186, 244) F5 rain drops
#define C_HOME_RAIN_CLOUD      0x428Cu  // (66, 80, 100) F5 clouds
#define C_HOME_RAIN_TOP        0x2167u  // (32, 44, 62) F5 sky gradient top
#define C_HOME_RAIN_BOT        0x0883u  // (12, 17, 26) F5 sky gradient bottom
#define C_HOME_F6_TOP          0x0842u  // (8, 10, 20) F6 bg gradient top
#define C_HOME_F6_BOT          0x0862u  // (10, 13, 22) F6 bg gradient bottom
#define C_HOME_AQI_GOOD            0x4752u
#define C_HOME_AQI_MODERATE        0xFE69u
#define C_HOME_AQI_USG             0xFCA7u
#define C_HOME_AQI_UNHEALTHY       0xFB4Du
#define C_HOME_AQI_VERY_UNHEALTHY  0xC39Fu
#define C_HOME_AQI_HAZARDOUS       0xB969u

// ---- per-app accents (single-accent theme: all equal C_ACC; HOME faces use C_HOME_ACCENT) ----
#define C_ACC_HOME      C_ACC
#define C_ACC_WEATHER   C_ACC
#define C_ACC_FORECAST  C_ACC
#define C_ACC_SKY       C_ACC
#define C_ACC_AIR       C_ACC
#define C_ACC_CRYPTO    C_ACC
#define C_ACC_STOCKS    C_ACC
#define C_ACC_NEWS      C_ACC
#define C_ACC_CRICKET   C_ACC
#define C_ACC_FOOTBALL  C_ACC
#define C_ACC_FOCUS     C_ACC
#define C_ACC_WORLD     C_ACC
#define C_ACC_SYSTEM    C_ACC
#define C_ACC_ORACLE    C_ACC
#define C_ACC_BUDDY     C_ACC

// ---- weather glyph layer tints (glyphs_wx_a4.h; muted, matte - no neon) ----
#define C_WX_SUN         0xEDECu  // (234, 190, 100) sun disc + rays, partly-cloudy sun
#define C_WX_MOON        0xCE9Cu  // (200, 208, 228) moon + star
#define C_WX_CLOUD       0xBE3Bu  // (184, 196, 216) front cloud
#define C_WX_CLOUD_DARK  0x7C54u  // (122, 136, 164) back cloud, heavy rain / thunder cloud
#define C_WX_RAIN        0x755Cu  // (112, 168, 224) rain + drizzle
#define C_WX_SNOW        0xE75Eu  // (226, 234, 246) snowflakes
#define C_WX_BOLT        0xEE2Bu  // (236, 198, 92) lightning bolt
#define C_WX_FOG         0x94F6u  // (146, 158, 182) fog lines, wind

// ---- AQI lookup ----
static const uint16_t AQI_BAND_COLORS[6] = {C_AQI_GOOD, C_AQI_MODERATE, C_AQI_USG, C_AQI_UNHEALTHY, C_AQI_VERY_UNHEALTHY, C_AQI_HAZARDOUS};
static const uint16_t AQI_BAND_HOME_COLORS[6] = {C_HOME_AQI_GOOD, C_HOME_AQI_MODERATE, C_HOME_AQI_USG, C_HOME_AQI_UNHEALTHY, C_HOME_AQI_VERY_UNHEALTHY, C_HOME_AQI_HAZARDOUS};
static const uint16_t AQI_BREAKS[7] = {0, 50, 100, 150, 200, 300, 500};   // band i covers (AQI_BREAKS[i], AQI_BREAKS[i+1]]

// ---- theme table (one entry today; index with THEME_DARK) ----
typedef struct {
  const char *name;
  uint16_t bg;
  uint16_t tile;
  uint16_t tile_top;
  uint16_t tile_hi;
  uint16_t tile_hi_top;
  uint16_t tile_rim;
  uint16_t line;
  uint16_t focus_border;
  uint16_t shadow;
  uint16_t txt;
  uint16_t icon;
  uint16_t mut;
  uint16_t dim;
  uint16_t acc;
  uint16_t last;
  uint16_t up;
  uint16_t down;
  uint16_t warn;
  uint16_t dot_idle;
  uint16_t home_bg;
  uint16_t home_surf;
  uint16_t home_txt;
  uint16_t home_mut;
  uint16_t home_dim;
  uint16_t home_acc;
} aura_theme_t;
#define THEME_DARK  0
#define THEME_COUNT 1
static const aura_theme_t AURA_THEMES[THEME_COUNT] = {
  { "Dark", C_BG, C_TILE, C_TILE_TOP, C_TILE_HI, C_TILE_HI_TOP, C_TILE_RIM, C_LINE, C_FOCUS_BORDER, C_SHADOW, C_TXT, C_ICON, C_MUT, C_DIM, C_ACC, C_LAST, C_UP, C_DOWN, C_WARN, C_DOT_IDLE, C_HOME_BG, C_HOME_SURF, C_HOME_TEXT, C_HOME_MUTED, C_HOME_DIM, C_HOME_ACCENT },
};

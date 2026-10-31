// aura_fonts.h - extern font objects (defined once in assets.cpp from fw_assets/fonts).
#pragma once
#include "src/assets/fonts/gfxfont_types.h"
#ifndef AURA_AAFONT_T
#define AURA_AAFONT_T
typedef struct { uint16_t offset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; } AAGlyph;
typedef struct { const uint8_t *bitmap; const AAGlyph *glyph; uint8_t first, last, yAdvance, bpp; } AAFont;
#endif
extern const GFXfont F_SB8, F_SB9, F_SB12, F_SB13, F_SB14, F_SB16, F_SB18, F_R10, F_R12, F_B22, F_B26, F_B34N,
    F_B46D, F_B58D, F_B74D;
extern const AAFont *const FAA_B34N, *const FAA_B46D, *const FAA_B58D, *const FAA_B74D;
// Role aliases (fonts_poppins.h per-screen table). assets.cpp gets the asset header's own set.
#ifndef AURA_ASSETS_TU
#define AF_MICRO      F_SB9
#define AF_MICRO_S    F_SB8
#define AF_BODY_S     F_R10
#define AF_BODY       F_R12
#define AF_LABEL      F_SB12
#define AF_LABEL_M    F_SB13
#define AF_ROW        F_SB14
#define AF_ROW_FOCUS  F_SB16
#define AF_TITLE      F_SB18
#define AF_NUM_M      F_B22
#define AF_NUM_L      F_B26
#define AF_NUM_XL     F_B34N
#define AF_WX_TEMP    F_B46D
#define AF_CLOCK_F2   F_B58D
#define AF_FLAP       F_B74D
#endif
#ifndef AF_DEG
#define AF_DEG   "\x7F"
#define AF_DOT   "\x80"
#define AF_ELL   "\x81"
#define AF_CHEV  "\x82"
#endif

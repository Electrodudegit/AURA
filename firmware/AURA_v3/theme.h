// theme.h - AURA v3.1 palette: Display UI theme_v3.h (single Dark theme) plus
// short aliases used by the UI code. All colours are plain RGB565.
#pragma once
#include "gfx.h"
#include "src/assets/theme_v3.h"

#define RGB565C(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_TILE_T   C_TILE_TOP
#define C_DN       C_DOWN
// home faces / boot (v1 graphite + aurora)
#define H_BG       C_HOME_BG
#define H_SURF     C_HOME_SURF
#define H_SURF2    C_HOME_SURF2
#define H_LINE     C_HOME_LINE
#define H_TXT      C_HOME_TEXT
#define H_MUT      C_HOME_MUTED
#define H_DIM      C_HOME_DIM
#define H_UP       C_HOME_UP
#define H_DN       C_HOME_DOWN
#define H_WARN     C_HOME_WARN
#define A_HOME     C_HOME_ACCENT
#define A_WEATHER  C_HOME_TEMP
#define A_FORECAST C_HOME_HUMID
#define A_SKY      RGB565C(206, 148, 255)
#define AUR0       C_HOME_AUR0
#define AUR1       C_HOME_AUR1
#define AUR2       C_HOME_AUR2
#define SUN_C      RGB565C(255, 216, 116)
#define RAIN_C     C_HOME_RAIN
#define AQI_COL    AQI_BAND_COLORS

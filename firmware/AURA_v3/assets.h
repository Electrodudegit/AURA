// assets.h - accessors for the Display UI assets (fonts, AA digits, A4 icons).
// Data lives in assets.cpp only (asset headers are static const).
#pragma once
#include <stdint.h>
#include "aura_fonts.h"

enum IconId : uint8_t {
  IC_HOME = 0, IC_WEATHER, IC_FORECAST, IC_SKY, IC_AIR, IC_CRYPTO, IC_STOCKS, IC_NEWS, IC_CRICKET, IC_FOOTBALL,
  IC_FOCUS, IC_WORLD, IC_SYSTEM, IC_ORACLE, IC_BUDDY, IC_MACRO, IC_SETTINGS, IC_COUNT  // 17 (MACRO/SETTINGS static)
};
// v3.1.6 PC Status icon masks live in icon_pc_a4.h. The launcher table is still 17 slots,
// so the menu uses the system glyph until that mask is linked in.
#define IC_PCSTATUS IC_SYSTEM

struct IconDef {
  const uint8_t *s16, *s24, *s48;            // static masks
  const uint8_t *base24, *base48;            // procedural icons (HOME, FOCUS): hand-less base mask
  const uint8_t *const *f24, *const *f48;    // 8 frames (frame 0 = static) for frame-animated icons
  const uint8_t *seq;                        // per 80 ms step: frame index
  uint8_t seq_len;
  bool procedural;
};
const IconDef *icon_def(uint8_t id);
#define ICON_STEP_MS_ 80

// Macro key glyphs (24 px A4): 0 PREV 1 PLAYPAUSE 2 NEXT 3 VOLDN 4 MUTE 5 VOLUP 6 BT
enum KeyGlyph : uint8_t { KG_PREV = 0, KG_PLAYPAUSE, KG_NEXT, KG_VOLDN, KG_MUTE, KG_VOLUP, KG_BT, KG_COUNT };
const uint8_t *key_glyph24(uint8_t i);
// State glyphs (48 px A4): 0 KEY 1 MONITOR 2 PHONE_WIFI
enum StateGlyph : uint8_t { SG_KEY = 0, SG_MONITOR, SG_PHONE_WIFI, SG_COUNT };
const uint8_t *state_glyph48(uint8_t i);
// Layered weather glyph (glyphs_wx_a4.h). Maps the WMO code with wx_from_wmo(); WIND is
// chosen when the code is 0-3 and wind >= WX_WIND_KMH. size 24/32/48 (48 falls back to 32).
// Draws centred on (cx, cy), layers back to front tinted with WX_TINT_RGB565[]. Returns the size drawn.
int wx_draw(int cx, int cy, uint8_t wmo, bool day, uint16_t wind_x10, int size, uint8_t alpha = 255);
uint8_t wx_glyph_id(uint8_t wmo, bool day, uint16_t wind_x10);
int wx_draw_id(int cx, int cy, uint8_t id, int size, uint8_t alpha = 255);

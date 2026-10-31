// assets.cpp - the ONLY translation unit that includes the Display UI data headers (static const data).
// Source: /workspace/aura_v3/fw_assets (copied verbatim into src/assets/).
#define AURA_ASSETS_TU 1
#include "assets.h"
#include "src/assets/fonts/fonts_poppins.h"
#include "src/assets/fonts/fonts_poppins_aa.h"
#include "src/assets/icons/icons_anim_a4.h"  // includes icons_a4.h
#include "src/assets/theme_v3.h"
#include "src/assets/icons/glyphs_ui_a4.h"
#include "src/assets/icons/glyphs_wx_a4.h"
#include "gfx.h"

const GFXfont F_SB8 = F_P_SB8, F_SB9 = F_P_SB9, F_SB12 = F_P_SB12, F_SB13 = F_P_SB13, F_SB14 = F_P_SB14,
    F_SB16 = F_P_SB16, F_SB18 = F_P_SB18, F_R10 = F_P_R10, F_R12 = F_P_R12, F_B22 = F_P_B22, F_B26 = F_P_B26,
    F_B34N = F_P_B34N, F_B46D = F_P_B46D, F_B58D = F_P_B58D, F_B74D = F_P_B74D;
const AAFont *const FAA_B34N = &F_PAA_B34N, *const FAA_B46D = &F_PAA_B46D, *const FAA_B58D = &F_PAA_B58D,
                   *const FAA_B74D = &F_PAA_B74D;
static const uint8_t *const k_null8[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

static const IconDef k_icons[ICON_COUNT] = {
  {ICON_HOME_16, ICON_HOME_24, ICON_HOME_48, ICON_HOME_24_BASE, ICON_HOME_48_BASE, k_null8, k_null8, ICON_HOME_SEQ, (uint8_t)sizeof(ICON_HOME_SEQ), true},
  {ICON_WEATHER_16, ICON_WEATHER_24, ICON_WEATHER_48, nullptr, nullptr, ICON_WEATHER_24_FRAMES, ICON_WEATHER_48_FRAMES, ICON_WEATHER_SEQ, (uint8_t)sizeof(ICON_WEATHER_SEQ), false},
  {ICON_FORECAST_16, ICON_FORECAST_24, ICON_FORECAST_48, nullptr, nullptr, ICON_FORECAST_24_FRAMES, ICON_FORECAST_48_FRAMES, ICON_FORECAST_SEQ, (uint8_t)sizeof(ICON_FORECAST_SEQ), false},
  {ICON_SKY_16, ICON_SKY_24, ICON_SKY_48, nullptr, nullptr, ICON_SKY_24_FRAMES, ICON_SKY_48_FRAMES, ICON_SKY_SEQ, (uint8_t)sizeof(ICON_SKY_SEQ), false},
  {ICON_AIR_16, ICON_AIR_24, ICON_AIR_48, nullptr, nullptr, ICON_AIR_24_FRAMES, ICON_AIR_48_FRAMES, ICON_AIR_SEQ, (uint8_t)sizeof(ICON_AIR_SEQ), false},
  {ICON_CRYPTO_16, ICON_CRYPTO_24, ICON_CRYPTO_48, nullptr, nullptr, ICON_CRYPTO_24_FRAMES, ICON_CRYPTO_48_FRAMES, ICON_CRYPTO_SEQ, (uint8_t)sizeof(ICON_CRYPTO_SEQ), false},
  {ICON_STOCKS_16, ICON_STOCKS_24, ICON_STOCKS_48, nullptr, nullptr, ICON_STOCKS_24_FRAMES, ICON_STOCKS_48_FRAMES, ICON_STOCKS_SEQ, (uint8_t)sizeof(ICON_STOCKS_SEQ), false},
  {ICON_NEWS_16, ICON_NEWS_24, ICON_NEWS_48, nullptr, nullptr, ICON_NEWS_24_FRAMES, ICON_NEWS_48_FRAMES, ICON_NEWS_SEQ, (uint8_t)sizeof(ICON_NEWS_SEQ), false},
  {ICON_CRICKET_16, ICON_CRICKET_24, ICON_CRICKET_48, nullptr, nullptr, ICON_CRICKET_24_FRAMES, ICON_CRICKET_48_FRAMES, ICON_CRICKET_SEQ, (uint8_t)sizeof(ICON_CRICKET_SEQ), false},
  {ICON_FOOTBALL_16, ICON_FOOTBALL_24, ICON_FOOTBALL_48, nullptr, nullptr, ICON_FOOTBALL_24_FRAMES, ICON_FOOTBALL_48_FRAMES, ICON_FOOTBALL_SEQ, (uint8_t)sizeof(ICON_FOOTBALL_SEQ), false},
  {ICON_FOCUS_16, ICON_FOCUS_24, ICON_FOCUS_48, ICON_FOCUS_24_BASE, ICON_FOCUS_48_BASE, k_null8, k_null8, ICON_FOCUS_SEQ, (uint8_t)sizeof(ICON_FOCUS_SEQ), true},
  {ICON_WORLD_16, ICON_WORLD_24, ICON_WORLD_48, nullptr, nullptr, ICON_WORLD_24_FRAMES, ICON_WORLD_48_FRAMES, ICON_WORLD_SEQ, (uint8_t)sizeof(ICON_WORLD_SEQ), false},
  {ICON_SYSTEM_16, ICON_SYSTEM_24, ICON_SYSTEM_48, nullptr, nullptr, ICON_SYSTEM_24_FRAMES, ICON_SYSTEM_48_FRAMES, ICON_SYSTEM_SEQ, (uint8_t)sizeof(ICON_SYSTEM_SEQ), false},
  {ICON_ORACLE_16, ICON_ORACLE_24, ICON_ORACLE_48, nullptr, nullptr, ICON_ORACLE_24_FRAMES, ICON_ORACLE_48_FRAMES, ICON_ORACLE_SEQ, (uint8_t)sizeof(ICON_ORACLE_SEQ), false},
  {ICON_BUDDY_16, ICON_BUDDY_24, ICON_BUDDY_48, nullptr, nullptr, ICON_BUDDY_24_FRAMES, ICON_BUDDY_48_FRAMES, ICON_BUDDY_SEQ, (uint8_t)sizeof(ICON_BUDDY_SEQ), false},
  {ICON_MACRO_16, ICON_MACRO_24, ICON_MACRO_48, nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},        // static
  {ICON_SETTINGS_16, ICON_SETTINGS_24, ICON_SETTINGS_48, nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},  // static
};
static_assert((int)ICON_IDX_MACRO == (int)IC_MACRO && (int)ICON_IDX_SETTINGS == (int)IC_SETTINGS, "icon index");
static_assert((int)KEY_GLYPH_COUNT == (int)KG_COUNT && (int)STATE_GLYPH_COUNT == (int)SG_COUNT, "glyph tables");
static_assert(ICON_COUNT == IC_COUNT, "icon order must match IconId");

const IconDef *icon_def(uint8_t id) { return id < IC_COUNT ? &k_icons[id] : nullptr; }

const uint8_t *key_glyph24(uint8_t i) { return i < KEY_GLYPH_COUNT ? GLYPHS_KEY_24[i] : nullptr; }
const uint8_t *state_glyph48(uint8_t i) { return i < STATE_GLYPH_COUNT ? GLYPHS_STATE_48[i] : nullptr; }
uint8_t wx_glyph_id(uint8_t wmo, bool day, uint16_t wind_x10) {
  if (wmo <= 3 && wind_x10 >= WX_WIND_KMH * 10) return WX_WIND;
  return (uint8_t)wx_from_wmo(wmo, day ? 1 : 0);
}
int wx_draw_id(int cx, int cy, uint8_t id, int size, uint8_t alpha) {
  if (id >= WX_GLYPH_COUNT) return 0;
  const WxGlyph *g = wx_glyph((wx_glyph_t)id, (uint8_t)(size >= 48 ? 48 : (size >= 32 ? 32 : 24)));
  if (!g || !g->n_layers) return 0;
  const int n = g->size;
  if (!gfx_vis(cy - n / 2, n)) return n;
  for (int i = 0; i < g->n_layers && i < 2; i++)
    gfx_a4(cx - n / 2, cy - n / 2, n, n, g->layer[i].a4, WX_TINT_RGB565[g->layer[i].tint], alpha);
  return n;
}
int wx_draw(int cx, int cy, uint8_t wmo, bool day, uint16_t wind_x10, int size, uint8_t alpha) {
  return wx_draw_id(cx, cy, wx_glyph_id(wmo, day, wind_x10), size, alpha);
}

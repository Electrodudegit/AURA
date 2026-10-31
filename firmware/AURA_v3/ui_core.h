// ui_core.h - AURA v3.1 screen manager, UI context and shared widgets.
// Host-compilable: depends only on gfx, theme, assets, data_store and ui_hal.
//
// Model: one active Screen. Per frame the UI task calls ui_frame_begin()
// (snapshot data + time, run input + tick), then for every band
// gfx_begin(...) + ui_draw(). draw() must not change state (it runs 12x per
// frame), so all animation is a pure function of g_ui.now.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "gfx.h"
#include "theme.h"
#include "assets.h"
#include "data_store.h"
#include "ui_hal.h"

// ------------------------------------------------------------------ input --
enum InType : uint8_t {
  IN_NONE = 0, IN_ROT, IN_CLICK, IN_HOLD,           // encoder: rotate (d = detents), press, hold 0.6 s
  IN_TAP, IN_LONG, IN_SWL, IN_SWR, IN_SWU, IN_SWD   // touch gestures (x, y = event position)
};
struct UiIn {
  uint8_t type;
  int16_t d;
  int16_t x, y, x0, y0;
};
// Raw touch for the calibration / diagnostics screens.
struct UiTouchRaw { bool down, fresh; int16_t x, y; uint16_t rx, ry, z; };

// ---------------------------------------------------------------- screens --
enum ScrId : uint8_t {
  SCR_BOOT = 0, SCR_HOME, SCR_MENU, SCR_APP, SCR_SETTINGS, SCR_BUDDY, SCR_SAVER, SCR_CAL, SCR_TGRID, SCR_COUNT
};
struct Screen {
  const char *name;
  void (*enter)(uint32_t now);
  void (*leave)(void);
  void (*input)(const UiIn &in, uint32_t now);
  void (*tick)(uint32_t now);
  void (*draw)(void);
  bool idle_buddy;     // idle 45 s here -> Buddy takes over
  bool raw_touch;      // wants UiTouchRaw instead of gestures (calibration)
  // v3.1.3 band skip: content key of band b (rows 20b..20b+19) for this frame, 0 = must draw.
  // Same non-zero key as the last drawn frame -> ui_task skips compose and SPI for that band.
  // Optional window: the key then covers only the columns OUTSIDE [w->x0, w->x1), and w->key
  // covers the window; if only w->key changed, just the window is composed and sent (CASET).
  uint32_t (*band_key)(int band, struct BandWin *w);
  // When non-null and true, this screen/page holds the display: no Buddy takeover.
  // The idle clock restarts when the hold ends. Per page (portal) or per app (focus, oracle, macro).
  // Trailing so existing positional initializers keep band_key in place (nullptr when omitted).
  bool (*idle_lock)(void);
};
struct BandWin { int16_t x0, x1; uint32_t key; };
uint32_t ui_band_key(int band, BandWin *w);  // 0 = draw (no screen key, toast, ...)
uint32_t w_status_key(bool home);    // content key of w_status() (battery, Wi-Fi, BLE dot)
uint32_t ui_hash(uint32_t h, uint32_t v);
uint32_t ui_hash_str(uint32_t h, const char *s);

// ------------------------------------------------------------------- apps --
enum AppId : uint8_t {
  APP_HOME = 0, APP_WEATHER, APP_FORECAST, APP_SKY, APP_AIR, APP_CRYPTO, APP_STOCKS, APP_NEWS, APP_CRICKET,
  APP_FOCUS, APP_WORLD, APP_SYSTEM, APP_ORACLE, APP_BUDDY, APP_MACRO, APP_PC, APP_SETTINGS, APP_COUNT  // PC before Settings (HMI v3.1.6)
};
struct AppDef {
  const char *name;      // grid / list title
  const char *title;     // status bar
  uint8_t icon;          // IconId
  const char *sub;       // list subtitle (focused row)
  void (*enter)(uint32_t now);
  void (*leave)(void);
  void (*input)(const UiIn &in, uint32_t now);
  void (*draw)(void);
};
const AppDef *app_def(uint8_t id);
// Live value for the menu tile / list row (e.g. "29°"), colour out.
void app_value(uint8_t id, char *buf, int n, uint16_t *col);

// ------------------------------------------------------------- UI context --
struct UiPrefs {
  uint8_t menu_list;    // 0 grid (default), 1 list
  uint8_t face;         // home face index 0..3 (F1, F2, F5, F6)
  uint8_t bright;       // backlight percent 10..100
  uint8_t night_dim;    // 1 = dim saver/buddy 22:30-06:30
  uint8_t last_app;     // menu focus memory
  uint8_t focus_min;    // focus preset minutes (15/25/50)
  uint8_t pc_keys;      // 1 = BLE HID 'PC keys' enabled (default ON)
  uint8_t tz;           // timezone table index (0 = IST). Old NVS blobs fail the size check and reload defaults.
  uint8_t macro_layer;  // 0 media, 1 Spotify (same HID media keys), 2 PC shortcuts
  uint8_t comp_on;      // 1 = local companion listener
};
struct UiCtx {
  uint32_t now;          // frame time (ms), frozen during draw
  uint32_t frame;
  AuraData d;            // data snapshot for this frame
  struct tm lt;          // local time
  bool tv;               // local time valid (NTP)
  uint32_t last_input;   // ms of the last user input
  uint8_t scr;           // ScrId
  uint8_t app;           // AppId while scr == SCR_APP
  uint8_t app_from;      // screen to return to from an app
  uint32_t scr_t0;       // when the current screen was entered
  bool buddy_idle;       // Buddy entered by the idle timer (not from the menu)
  UiPrefs prefs;
  UiTouchRaw raw;
  bool night;            // inside the night window
};
extern UiCtx g_ui;

void ui_init(uint32_t now);
// Snapshot + time + idle/night logic; then process queued inputs. Call once per frame.
void ui_frame_begin(uint32_t now, const AuraData *d, const struct tm *lt, bool tv);
void ui_input(const UiIn &in);
void ui_touch_raw(const UiTouchRaw &r);
void ui_draw(void);                      // current band (after gfx_begin)
bool ui_wants_raw_touch(void);
const char *ui_screen_name(void);
bool ui_portal_secret_visible(void);  // setup portal page (AP password / join QR) on screen
bool ui_secret_on_screen(void);        // portal password or companion pairing token on screen
bool ui_is_back(const UiIn &in);       // hold, left-edge swipe, or the 44 px top-left target
uint32_t ui_frame_period_ms(void);     // 25 fps while active; slower on a static idle screen
bool ui_light_sleep_ok(void);          // radios down and the UI is idle: a short light sleep is safe
void ui_go(uint8_t scr);
void ui_open_app(uint8_t app);
void ui_back(void);                      // app -> menu, menu -> home
void ui_toast(const char *msg, uint32_t ms);
void ui_prefs_changed(void);             // persist (debounced by the HAL)
void ui_backlight_refresh(void);         // apply prefs.bright to the panel immediately

// --------------------------------------------------------------- widgets --
uint32_t ui_elapsed(void);               // ms since the screen was entered
// Status bar (matte v2): accent tick, NAME, right side: badge, Wi-Fi, battery.
// badge: nullptr = auto (job age for the app's job), "" = none.
void w_status(const char *name, const char *badge = nullptr, bool home_style = false);
void w_card(int x, int y, int w, int h, int r = 10, bool hi = false, bool outline = false);
int w_chip(int x, int y, const char *s, bool on, uint16_t c = C_ACC);
void w_footer(const char *s);
void w_page_dots(int idx, int n, int y, uint16_t on_col, uint16_t off_col);
void w_battery(int x, int y, int pct, uint16_t c);
void w_wifi(int cx, int cy, int lvl, uint16_t on, uint16_t off);
void w_icon(uint8_t icon, int size, int x, int y, uint16_t c, uint32_t step = 0, float scale = 1.0f);
void w_spinner(int cx, int cy, int r, uint16_t c, uint16_t bg);
// Weather glyphs (port of the mockup mini_glyph / cloud / sun / moon).
void w_moon(float cx, float cy, float r, float phase, uint16_t bg);  // Sky: moon PHASE (astronomy, not a condition)
// Weather condition glyph: A4 masks from glyphs_wx_a4.h, centred, size 24/32/48.
void w_wx_glyph(int cx, int cy, uint8_t wmo, bool day, int size = 24, uint16_t wind_x10 = 0);
uint8_t wx_kind(uint8_t wmo);             // 0 clear, 1 cloudy, 2 rain, 3 storm, 4 snow, 5 fog
const char *wx_text(uint8_t wmo);
// States (mockups/state_*.png)
void st_loading(void);
void st_missing_key(const char *what);
void st_empty(const char *title, const char *l1, const char *l2, uint8_t icon);
void st_retrying(const char *title, const JobInfo *j);
void st_needs_pc(const char *what);
void st_offline(void);
// Common job gate: returns true if the app content can be drawn; otherwise draws
// the right state (loading / retrying / offline) and returns false.
bool w_job_gate(uint8_t job, bool have_data);
void w_stale_pill(uint8_t job, int x, int y);
// Formatting
void fmt_temp(char *b, int n, int16_t x10);           // "29°"
void fmt_hm(char *b, int n, int minutes);             // "18:02"
void fmt_age(char *b, int n, uint32_t ok_ms);         // "2M", "12 MIN AGO"
uint32_t job_age_min(uint8_t job);
float ease_out(float u);                              // cubic
float ui_clampf(float v, float lo, float hi);
// Text that never overflows: truncated with an ellipsis to maxw px (Poppins runs ~8-10 % wide).
int w_text_ell(const GFXfont *f, int x, int ym, const char *s, int maxw, uint16_t c, GfxAlign al = GA_L);
// Toast overlay (drawn by ui_draw on top of every screen)
void w_toast_draw(void);

// ------------------------------------------------- per-screen entry points --
extern const Screen SCR_BOOT_DEF, SCR_HOME_DEF, SCR_MENU_DEF, SCR_APP_DEF, SCR_SETTINGS_DEF, SCR_BUDDY_DEF,
    SCR_SAVER_DEF, SCR_CAL_DEF, SCR_TGRID_DEF;
bool boot_done(void);
// Buddy v2 face rig (v3.1.4): shared by the Buddy app, the idle takeover, F2, Oracle and states.
#include "buddy.h"

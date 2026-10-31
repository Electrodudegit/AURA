// face_specs.h - AURA v3.1.0 numeric UI spec (home faces F1/D2, F2, F5, F6; menu grid/list; status bar).
// Extracted from src/home_faces.py, home_faces_v2.py, home_concepts.py, screens.py, menu_v2.py, menu_final.py,
// anim_icons.py, theme_v2.py. Colours are theme_v2.h names. Rects are x, y, w, h (px). Times in ms.
// Values marked [mockup] come from 12.5 fps preview code; device target is 25 fps (scale frame counts, keep ms).
#pragma once
#include <stdint.h>
namespace aura { namespace spec {
struct Rect { int16_t x, y, w, h; };
struct Pt { int16_t x, y; };

// ===================== STATUS BAR (home + menu + apps) =====================
constexpr Rect STATUS        = {0, 0, 320, 24};      // fill BG (home: C_H_BG, v2: C_BG)
constexpr int  STATUS_RULE_Y = 23;                     // home: aurora-gradient line (aur_grad(x) at 43% over BG); v2: C_LINE
constexpr Rect STATUS_TICK   = {12, 7, 3, 10};       // home & icon-less screens: accent tick, radius 1
constexpr Rect STATUS_ICON16 = {9, 4, 16, 16};       // v2 apps/menu: ICONS_16 tinted C_ACCENT, name starts x=31
constexpr Pt   STATUS_NAME   = {21, 7};              // AF_MICRO, top-left; C_TEXT; then +10 px gap -> 'EXAMPLE' AF_MICRO_S C_EXAMPLE_TAG
constexpr Rect STATUS_BATT   = {288, 7, 19, 10};     // shell 18x9 r2 outline C_MUTED, nub (306,10..13) 2x4, fill x+2..x+16 by %, C_TEXT (home: C_H_UP)
constexpr int  STATUS_PCT_RX = 283;                    // '88%' AF_MICRO right-aligned, C_MUTED
constexpr int  STATUS_WIFI_X = 251, STATUS_WIFI_Y = 17; // 3 arcs r=5/8/11, width 2, 225..315 deg, + 3 px dot; lvl0: DOWN slash (xw-6,6)-(xw+6,17)
// menu: position text 'P1/3 . 06/15' AF_MICRO_S right-aligned at x = WIFI_X-16 = 235

// ===================== HOME COMMON =====================
constexpr int  HOME_DOTS_Y = 234;   // 4 dots F1,F2,F5,F6: active pill 14x4 r2 C_H_ACCENT, idle 4x4 C_H_MUTED@43%; step 10, 18 after pill
constexpr int  HOME_FACES = 4;
// Aurora (screens.aurora): 3 ribbons, 2-px columns, line-table additive over BG.
struct AuroraRibbon { uint8_t r, g, b; float phase; int16_t base_y; int16_t len; uint8_t peak; };
constexpr AuroraRibbon AURORA[3] = {
  {40, 205, 255, 0.0f, 18, 36, 150},   // ribbon i: base_y = 18 + 15*i, len = 36 - 7*i, peak alpha (150 - 28*i)/255
  {190, 130, 255, 2.1f, 33, 29, 122},
  {60, 238, 170, 4.2f, 48, 22, 94} };
// y(x) = base_y + 11*sin(0.021x + 1.15T + ph) + 5*sin(0.052x - 0.75T + ph)
// intensity(x) = 0.55 + 0.45*sin(0.09x + 1.9T + 1.7ph)
// vertical profile: row k (k = row>>1, 0..19): f = ((len - 2k)/len)^2 * peak, added colour = f * ribbonRGB * intensity
// bottom 2 rows of each ribbon + (6,30,16)*intensity (green fringe). 34 stars: sx = s%320, sy = (s>>9)%(h-4),
//   s = (i+7)*2654435761 (u32); twinkle tw = sin(2T + i): >0.55 TEXT, >-0.2 DIM, else DIM@47%.
// boost: out = BG + (aurora - BG) * BOOST (clip 0..255)
struct AuroraUse { int16_t y0, h; float t_scale, boost; };
constexpr AuroraUse AUR_F1 = {24, 126, 2.1f, 1.45f};    // T = 2.1 * seconds
constexpr AuroraUse AUR_F2 = {150, 60, 2.0f, 0.80f};
// line-table gradient (one colour per row): F1/D2 rows 150..239 C_H_BG -> C_H_BOTTOM; F6 rows 24..239 C_H_F6_TOP -> C_H_F6_BOT;
// F5 sky rows 24..239 C_H_RAIN_TOP -> C_H_RAIN_BOT. aur_grad(x) = cyan->violet over x 0..160, violet->green 160..320.
// Particles: rise, wobble, twinkle. y = y0 + ((y_seed - 24 - v*t) mod h); x = x_seed + 3*sin(1.3t + x_seed);
//   tw = 0.5 + 0.5*sin(5t + x_seed); colour = BG -> AUR[c] at (80 + 150*tw)/255; 1 px, plus (x+1,y),(x,y+1) when tw > 0.8.
struct Particle { int16_t x, y; float v; uint8_t c; };   // v in px/s
constexpr Particle PARTICLES[40] = {
  {270, 87, 10.6f, 0}, {43, 55, 11.7f, 1}, {306, 147, 10.5f, 2}, {76, 102, 10.7f, 0},
  {76, 139, 8.6f, 1}, {199, 35, 7.1f, 2}, {254, 146, 15.7f, 0}, {104, 29, 9.6f, 1},
  {73, 97, 11.2f, 2}, {148, 52, 7.8f, 0}, {183, 63, 12.8f, 1}, {145, 86, 15.1f, 2},
  {152, 141, 7.6f, 0}, {83, 80, 15.6f, 1}, {34, 84, 15.2f, 2}, {309, 76, 13.1f, 0},
  {116, 33, 9.8f, 1}, {167, 33, 13.5f, 2}, {58, 117, 12.5f, 0}, {217, 110, 7.1f, 1},
  {290, 139, 11.5f, 2}, {53, 65, 11.4f, 0}, {307, 33, 6.1f, 1}, {296, 59, 6.8f, 2},
  {228, 132, 11.7f, 0}, {121, 81, 11.3f, 1}, {239, 71, 11.5f, 2}, {208, 125, 9.3f, 0},
  {36, 127, 12.8f, 1}, {175, 42, 12.4f, 2}, {173, 58, 8.1f, 0}, {4, 88, 10.6f, 1},
  {25, 42, 9.8f, 2}, {183, 87, 12.4f, 0}, {169, 116, 6.4f, 1}, {176, 49, 10.1f, 2},
  {105, 124, 8.7f, 0}, {218, 42, 14.9f, 1}, {205, 74, 9.5f, 2}, {67, 34, 11.5f, 0} };
struct ParticleUse { int16_t y0, h; uint8_t n; float t_scale; };
constexpr ParticleUse PART_F1 = {24, 126, 40, 1.0f}, PART_F2 = {150, 60, 24, 1.0f}, PART_F6 = {24, 210, 30, 0.5f};

// ===================== F1 / D2 SPLIT-FLAP AURORA (default) =====================
constexpr int  FLAP_W = 70, FLAP_H = 92, FLAP_Y = 30, FLAP_R = 11;
constexpr int  FLAP_X[4] = {4, 78, 166, 240};
constexpr uint8_t FLAP_ALPHA = 214;          // tile face alpha over aurora (~16% aurora shows through)
// tile: bottom C_H_FLAP_BOT, top half rows 0..h/2-3 C_H_FLAP_TOP (h=92), rows h/2-2..end C_H_FLAP_BOT; rows 4..h/2-3, x 3..w-4 blend TOP->C_H_FLAP_GLOSS at ((46-y)/46)*90/255;
//   rim (10,1)-(w-11,1) (170,190,220)@43%; gloss polygon (5,5),(26,5),(12,h-5),(5,h-5) white@5%;
//   hinge: 2-px C_H_FLAP_HINGE at y=45..46, 1-px C_H_FLAP_HINGE_LT at y=47 (x 2..w-3); digit AF_FLAP white centred at (35,46) middle-middle, shadow copy +3 px down in C_H_FLAP_HINGE.
//   pins: 5x12 r2 C_H_PIN at x-3 and x+w-3, y mid-6..mid+6, 1-px C_H_PIN_LT highlight.
//   shadow: black rounded rects offset (+1,+4), grown 3/2/0 px, radius 12+g, alpha 40/55/90. Baked glow C_H_ACCENT 3/2/1 px at 12/22/38 %.
constexpr int  COLON_X = 157, COLON_DY = 13, COLON_R = 4;   // centres (157, 76 +/- 13); blink 500 ms on / 500 ms dim (C_H_ACCENT@39%)
// Flip (minute change): 500 ms; u = t/500 in 0..1.
//   u < 0.5 : old top flap folds: k = cos(pi*u), flap height = 46*k anchored at hinge, shade alpha 170*(1-k) top -> 90*(1-k) bottom;
//             revealed new top shaded 0 -> 150*k toward hinge.
//   u >= 0.5: new bottom flap drops: k = -cos(pi*u), height 46*k from hinge, shade 110*(1-k) -> 0; old bottom darkens 160*(1-k)->60*(1-k);
//             leading-edge glint line (170,190,220) alpha 160*(1-k).
constexpr uint16_t FLIP_MS = 500; constexpr uint8_t FLIP_FRAMES_25FPS = 13;  // [mockup] 12.5 fps -> 6 frames
// Date line y=139 (centre): 'TUESDAY' AF_LABEL_M tracked +1.8 px C_H_TEXT, 3-px dot C_H_ACCENT, '20 OCTOBER' AF_BODY tracked +1.2 C_H_MUTED; block centred on x=160.
// Seconds bar
constexpr int  SEC_X0 = 16, SEC_X1 = 304, SEC_Y = 154;   // track C_H_TEXT@13% over BG; progress = aur_grad(x-16 over 288)@82%; head dot r2 C_H_TEXT
// Gauges (TEMP 27/45 C_H_TEMP, HUMID 64/100 C_H_HUMID, AQI 72/200 C_H_WARN)
constexpr int  GAUGE_CX[3] = {56, 160, 264}, GAUGE_CY = 184, GAUGE_R = 21, GAUGE_THICK = 4;
constexpr int  GAUGE_A0 = 135, GAUGE_SWEEP = 270;        // degrees, 0 = +x, clockwise (screen y down); track C_H_SURF2
constexpr int  GAUGE_TICKS = 11, GAUGE_TICK_STEP = 27;   // ticks r+5..r+8 (major k%5==0, C_H_MUTED) / r+5..r+7 (minor, C_H_DIM@59%)
constexpr uint16_t GAUGE_COUNTUP_MS = 1100;              // e = 1-(1-t/1100)^4 (ease-out quart), value and arc together
// knob: r3 C_H_TEXT at arc end. After 1200 ms: 3-dot comet along the filled arc, 120 deg/s (wraps within filled sweep), dots r 2/1.5/1, 7 deg apart, colour gauge->C_H_TEXT 78/51/24 %.
// value AF_LABEL_M centred at (cx, 184); label AF_MICRO_S centred at y=198 C_H_DIM. Footer AF_MICRO_S centred y=214 C_H_MUTED.
// Dirty regions (per frame): aurora band rows 24..149 (full width, animated) + seconds bar row 152..156 + gauge comets after count-up;
//   flip tile rect (x,30,70,92) during flip. [estimate] 37.7% avg / 44.8% max of screen, 6.8 bands avg, SPI 11.6 / 13.8 ms.

// ===================== F2 BUDDY CLOCK =====================
constexpr Rect F2_FACE = {52, 26, 216, 112};   // face rendered at 320x240 (eyes 92x92, centres x=160+/-75, y=110), crop (14,44)-(306,196), scaled 0.74, composited 'lighter'
// resulting on-screen eyes ~68x68, centres (104.6,74.6) and (215.4,74.6); colour CYAN (80,220,255); mouth y ~ 26+(170-44)*0.737 = 119
constexpr Pt   F2_TIME = {160, 166};           // AF_CLOCK_F2 centred, white with 2-px (6,8,14) stroke + glow C_H_ACCENT 3/2/1 px @ 15/28/43 %
constexpr int  F2_INFO_Y = 204;                 // 'TUE 20 OCT . 27 deg . CLEAR' AF_MICRO centred (220,228,240)
// Loop 3000 ms: blink centred at 1100 ms, half-width 120 ms (triangular lid close); glance left px=-0.85 from 1600 ms (150-ms ramp),
// right +0.85 at 2200 ms (150-ms ramp), return to 0 from 2750 ms over 200 ms. Pupil offset px in eye-width units.
// Dirty: face rect + aurora strip rows 150..209 (animated). [estimate] 41.2% avg, SPI 12.7 ms.

// ===================== F5 WEATHER SCENE =====================
constexpr Rect F5_SKY = {0, 24, 320, 216};     // gradient C_H_RAIN_TOP -> C_H_RAIN_BOT
// clouds [summary of wx_scene, verify in capture]: colour C_H_RAIN_CLOUD (far cloud = blend to bottom 43%); s=12 at y 50, s=15 at y 68, s=13 at y 56; drift 10 px/s and 5 px/s (x1.2 time scale)
// rain: 26 drops (fixed seed), line (x,y)-(x-3,y+9) colour C_H_RAIN, speed 60..120 px/s * 1.2, wrap in h-8
constexpr Pt   F5_TIME = {160, 84};            // AF_FLAP (72 in mockup -> 74 digits font), glow C_H_HUMID 0.7
constexpr int  F5_DATE_Y = 128;                 // AF_MICRO centred (225,232,242)
constexpr int  F5_RULE_Y = 150;                 // 1-px aur_grad @31% over (12,17,26)
constexpr Rect F5_STRIP = {8, 160, 305, 63};   // glass: 60% BG over scene, r10, outline C_H_HUMID@43%
constexpr int  F5_DAY_X0 = 18, F5_DAY_STEP = 74; // per day: label AF_MICRO_S centred (x+28,166); glyph centre (x+16,192); hi AF_LABEL left-middle (x+40,186) C_H_TEXT; lo AF_BODY_S left-middle (x+40,203) C_H_DIM; label TODAY C_H_TEXT, others C_H_MUTED
// Dirty: rain + clouds over the sky; strip static. [estimate] 14.3% avg / 18.1% max, SPI 4.4 ms.

// ===================== F6 ANALOG AURORA =====================
constexpr int  F6_CX = 160, F6_CY = 130, F6_R = 96;
// ring: 120 arcs of 3 deg, width 2, colour aur_grad mirrored, brightness BG->c at (90 + 60*sin(2t + 0.1k))/255
// ticks: 60; major (k%5==0) from R-12 to R-4 width 3 (k%15==0 C_H_TEXT else C_H_MUTED); minor R-7..R-4 width 1 C_H_DIM@55%
// numerals 12/3/6/9: AF_ROW centred at radius R-26 = 70, C_H_MUTED
struct Hand { int16_t len, width, tail; uint8_t aur; };
constexpr Hand F6_HOUR = {50, 6, 10, 0}, F6_MIN = {74, 4, 10, 1}, F6_SEC = {82, 2, 10, 2};  // colour = AUR[i] blended 35% to white + baked glow
// hub r5 C_H_TEXT + r2 AUR[2]. Draw order: hour, minute, time chip, date, seconds, hub.
constexpr int  F6_CHIP_W = 56, F6_CHIP_H = 16, F6_CHIP_R = 5;   // fill C_H_SURF, outline C_H_SURF->C_H_LINE 63%, text AF_LABEL C_H_TEXT
// Placement rule (evaluate once per minute): candidates (angle from 12 o'clock, offset of chip centre, penalty deg)
struct Slot { int16_t ang, dx, dy, penalty; };
constexpr Slot F6_SLOTS[4] = { {180, 0, 50, 0}, {0, 0, -46, 0}, {90, 37, 0, 20}, {270, -37, 0, 20} };  // bottom dy=48+2
// pick slot maximising min(angdist(slot, hourAngle), angdist(slot, minuteAngle)) - penalty;
// hourAngle = (h%12 + m/60)*30, minuteAngle = (m + s/60)*6; date 'TUE 20' AF_MICRO_S C_H_DIM at chip centre y-20 (bottom slot) else y+13.
// Dirty: seconds hand sweep + ring shimmer + particles. [estimate] 20.4% avg / 23.6% max, SPI 6.3 ms.

// ===================== MENU GRID (default) =====================
constexpr int  GRID_TILE_W = 96, GRID_TILE_H = 88, GRID_R = 10;
constexpr int  GRID_X[3] = {8, 112, 216}, GRID_Y[2] = {30, 126};
// tile: C_TILE, top half (0..44) C_TILE_TOP, rim line (x+10..x+86, y) C_TILE_RIM.
// content: icon 48 centre (x+48, y+33); name AF_LABEL centred y+64; value AF_MICRO_S centred y+72 (semantic colour).
// focus (lifted): shadow rect (+1,+3) C_SHADOW; tile drawn 1 px up with C_TILE_HI / C_TILE_HI_TOP; 1-px border C_ACCENT_BORDER;
//   underline (x+36, y+81, 25, 3) r1 C_ACCENT; icon C_TEXT + living animation; content shifted -1 px.
constexpr Rect GRID_LAST_CHIP = {60, 5, 29, 12};  // relative to tile; r5, fill C_LAST_FILL, outline C_LAST_OUTLINE, 'LAST' AF_MICRO_S C_LAST
constexpr int  GRID_DOTS_Y = 226;                   // 3 pages; active pill C_ACCENT
// page 3 empty area hint: 'PRESS OPEN . HOLD BACK . SWIPE PAGE' AF_MICRO_S centred y=170 C_DIM@78%
// MOTION
constexpr uint16_t FOCUS_SLIDE_MS = 140;   // plate rect lerp old->new tile, e(u) = 1 - (1-u)^3  [mockup: 4 frames @25 fps = 160 ms]
constexpr uint8_t  HOP_STEPS = 5;          // 40-ms steps: scale 1.05, 1.08, 1.05, 1.02, 1.00 (160 ms of motion + settle), y -= 2*(s-1)/0.08
constexpr float    HOP_SCALE[5] = {1.05f, 1.08f, 1.05f, 1.02f, 1.00f};
constexpr uint16_t PAGE_SLIDE_MS = 200;    // 5 frames, ease-out cubic, tile area rows 24..235 shifted by 320*e(u)
constexpr uint16_t PAGE_FLAP_MS = 320;     // split-flap stagger: tile j starts at frame 0.5*j, 3 frames each; squash s=|cos(pi*u)|, swap content at u=0.5
// [estimate] focus move 16.1% avg / 25.9% max (4.9 / 8.0 ms); slide 78/81% (24.0/25.0 ms); crossfade 69/73% (21.3/22.4 ms); flap 31.8% avg (9.8 ms)

// ===================== MENU LIST (setting) =====================
constexpr int  LIST_VP0 = 24, LIST_VP1 = 240, LIST_ROW = 40, LIST_HDR = 22, LIST_PADT = 4;
constexpr int  LIST_GLASS_X0 = 8, LIST_GLASS_X1 = 305, LIST_GLASS_H = 42, LIST_GLASS_R = 10;
constexpr int  LIST_ICON_CX = 34, LIST_NAME_X = 58, LIST_VALUE_RX = 296;
constexpr int  LIST_RAIL_X = 313, LIST_RAIL_Y0 = 30, LIST_RAIL_Y1 = 234, LIST_THUMB_MIN = 18;   // track 3 px C_LINE, thumb 5 px C_ACCENT
constexpr int  LIST_FADE_TOP = 6, LIST_FADE_BOTTOM = 10;
// row centres in content space: 46,86,126,166,206 | 268,308 | 370,410,450 | 512,552,592,632,672; headers at 4,226,328,470; content h 700
// focused: name AF_ROW_FOCUS (NX, cy-7), subtitle AF_BODY_S (NX, cy+9), value AF_ROW_FOCUS right (VX, cy-6), label AF_MICRO_S right cy+6;
//   accent bar (12, cy-10, 3, 21). Idle: name AF_ROW, value AF_MICRO right, icon C_ICON.
// scroll only when focus within (ROW + GLASS_H/2 - 6) of an edge; scroll and glass ease together (140 ms ease-out cubic).
// [estimate] focus move 44.8% avg / 79.4% max (incl. scroll steps), SPI 13.8 / 24.4 ms

// ===================== LIVING ICONS =====================
constexpr uint16_t ICON_STEP_MS = 80;     // 12.5 fps; see icons_anim_a4.h ICON_SEQ / ICON_LOOP_MS
constexpr Rect ICON_DIRTY_GRID = {-2, -4, 52, 52};   // relative to the 48-px icon top-left (hop margin)
constexpr Rect ICON_DIRTY_LIST = {-2, -2, 28, 28};
} } // namespace aura::spec

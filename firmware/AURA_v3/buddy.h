// buddy.h - Buddy v2 face (AURA v3.1.4, specs/BUDDY_V2_SPEC.md): 10 expressions + IDLE, a 240 ms
// parameter morph, a time-of-day weighted picker, input reactions and the Oracle think hook.
// All state is one static rig (.bss) shared by F2, the Buddy app / idle takeover and Oracle.
// No heap. Rendering is band-safe (called once per band) and reads only the per-frame
// snapshot that buddy_tick() computes, so a repeated frame key means identical pixels; the
// screens turn that into band-skip windows (ui_task band keys, v3.1.3).
#pragma once
#include <stdint.h>

struct BandWin;

struct BuddyParams {  // 18 floats, all lerped by the morph (px values are at s = 1)
  float eh, ew, eh_r, ew_r;                      // eye height / width scale, left and right
  float lid_top, lid_ang, lid_top_r;             // top lid cut 0..1 (L, R), angle deg (+ = inner corner low)
  float lid_bot, lid_bot_r;                      // bottom (cheek) cut 0..1, L and R
  float ix, iy, iris;                            // iris offset px, radius scale
  float m_w, m_curve, m_open, m_x;               // mouth width, curve (+ smile), open radius, x offset
  float heart, eye_a;                            // eye -> heart 0..1, eye colour alpha over bg 0..255
};
enum BuddyExpr : uint8_t { BX_IDLE = 0, BX_HAPPY, BX_SLEEPY, BX_CURIOUS, BX_SURPRISED, BX_BORED, BX_LOVE,
                           BX_THINKING, BX_SAD, BX_WINK, BX_MISCHIEF, BX_COUNT };
enum BuddyState : uint8_t { BS_IDLE = 0, BS_EXPR, BS_REACT, BS_THINK, BS_OFFLINE };
enum BuddyOutcome : uint8_t { BO_OK = 0, BO_ERR, BO_BUSY };  // -> happy 1.2 s / sad 1.5 s / bored 2 s
enum BuddyInput : uint8_t { BI_TAP = 1, BI_PRESS, BI_ROT, BI_SWIPE };
enum BuddyMode : uint8_t { BRM_APP = 0, BRM_F2 };
enum BuddySpecial : uint8_t { BSP_NONE = 0, BSP_WOOZY, BSP_KO };

// What one frame draws (computed once per frame by buddy_tick; hashed into the band keys).
struct BuddyFrame {
  BuddyParams p;
  float dx, dy, scale, blink;   // face offset px (whole px), pop scale, blink 0..1 (1/16 steps)
  float gx, gy;                 // saccade + glance + look iris offset px (whole px)
  float acc_in, acc_out;        // accent alpha of expr / out_expr (1/8 steps)
  float heart_k;                // love heart pulse 1.00..1.08
  int8_t bh;                    // breathing: eye height +-px
  uint8_t expr, out_expr, special, dim, pad[3];
  uint16_t at, out_at;          // ms since expr / out_expr accents started (40 ms steps, capped)
};

struct BuddyRig {
  BuddyFrame fr;                // current frame (cur params live in fr.p)
  BuddyParams from;             // morph start (the target is k_bx[expr])
  uint32_t m_t0, expr_t0, out_t0;
  uint16_t m_dur;
  uint8_t mode, expr, prev, state, hist[4], special, pokes, think, speak, offline, react_expr, browse, snap;
  uint32_t next_pick, dwell_end, blink_t0, next_blink, dbl_at, sacc_t0, sacc_hold, look_t0, look_hold, next_look;
  uint32_t react_t0, react_end, poke_end, browse_end, last_poke, twitch_t0, next_twitch, glance_t0;
  float sx0, sy0, sx1, sy1, lx, ly;
  int8_t glance_dir;
  uint32_t rng;
  int16_t bb_old[4], bb_new[4], dirty[4];  // x0,y0,x1,y1 (px, exclusive) of the last / this frame and their union
  uint32_t fkey;                           // hash of fr (band-key window key)
  float cx, cy, s;                         // where the owning screen draws it (for the bounds)
  float pop_max;                           // v3.1.5: pop scale clamp for this placement (buddy_pop_max)
};

extern const BuddyParams k_bx[BX_COUNT];  // spec §3 table (flash)
// One rig in .bss (~370 B): Home F2, the Buddy app, the idle takeover and Oracle are never on
// screen together, and every one of them re-inits it (with its mode) in its enter().
extern BuddyRig g_rig;

void buddy_rig_init(BuddyRig *r, uint8_t mode, uint32_t now);
void buddy_place(BuddyRig *r, float cx, float cy, float s);           // where it is drawn (bounds, pop clamp)
// v3.1.5: REACT pop grows each eye about its own centre, up to BUDDY_POP_SPEC, clamped per placement so the face
// top stays at y >= BUDDY_FACE_TOP_MIN (worst case; see buddy_face.cpp). top_at_max: that worst-case top.
#define BUDDY_POP_SPEC 1.10f
#define BUDDY_FACE_TOP_MIN 26.0f
float buddy_pop_max(uint8_t mode, float cy, float s, float *top_at_max);
void buddy_tick(BuddyRig *r, uint32_t now);                           // picker, blink, saccade, look, morph
void buddy_input(BuddyRig *r, uint8_t in_type, int d, uint32_t now);  // REACT / poke / browse / glance
void buddy_render(const BuddyRig *r, float cx, float cy, float s, uint16_t bg);  // band-safe
void buddy_bounds(const BuddyRig *r, float cx, float cy, float s, int16_t out[4]);
void buddy_think_begin(BuddyRig *r, uint32_t now);
void buddy_think_end(BuddyRig *r, uint32_t now, uint8_t outcome);
void buddy_speak(BuddyRig *r, bool on);
// Stateless still of one expression (offline card etc.).
void buddy_draw_expr(uint8_t bx, float cx, float cy, float s, uint16_t bg);
const char *buddy_caption(const BuddyRig *r);
bool buddy_on_screen(void);  // Buddy app, idle takeover or Home F2 (ui_task: press -> frame at once)
const char *buddy_expr_name(uint8_t bx);
// Band key helper for full-screen Buddy views: rows touched by the rig's dirty rect get a
// column window keyed by the frame hash; every other band returns base (static content).
uint32_t buddy_band_key(const BuddyRig *r, int band, BandWin *w, uint32_t base);
// Host captures: pin the app rig to an expression for a frame at time now (morph_u < 0:
// settled; 0..1: that point of the 240 ms morph from IDLE). bx < 0 releases it.
void buddy_debug_set(int bx, float morph_u, uint32_t now);

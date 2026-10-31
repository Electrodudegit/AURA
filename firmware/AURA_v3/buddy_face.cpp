// buddy_face.cpp - Buddy v2 (v3.1.4, specs/BUDDY_V2_SPEC.md): pet-robot face with 10
// expressions + IDLE driven by a parameter rig (morph, picker, blink, saccades, look-around,
// breathing, reactions, Oracle think), the idle takeover screen, the Buddy app and the
// split-flap saver. The 27-mood v2 drawing code is gone; WOOZY and K.O. stay as poke specials.
#include "prof.h"
#include "apps.h"
#include "buddy.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BX_BLUSH 0xFBD2u
#define STARC RGB565C(255, 214, 80)

// ------------------------------------------------------------- §3 table --
//  eh    ew    eh_r  ew_r  top   ang   top_r bot   bot_r ix    iy    iris  m_w   curve open  m_x   heart eye_a
const BuddyParams k_bx[BX_COUNT] = {
  {1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.0f, 0.00f, 0.16f, 0.16f, 0.0f, 0.0f, 1.00f, 18, 4, 0, 0, 0, 255},   // IDLE
  {0.55f, 1.04f, 0.55f, 1.04f, 0.00f, 0.0f, 0.00f, 0.50f, 0.50f, 0.0f, 0.0f, 0.00f, 20, 6, 0, 0, 0, 255},   // happy
  {1.00f, 1.00f, 1.00f, 1.00f, 0.55f, -6.f, 0.55f, 0.12f, 0.12f, 0.0f, 6.0f, 0.90f, 10, 0, 0, 0, 0, 200},   // sleepy
  {1.08f, 1.00f, 0.92f, 1.00f, 0.00f, 10.f, 0.12f, 0.16f, 0.16f, 8.0f, -4.f, 1.00f, 8, 0, 3, 6, 0, 255},    // curious
  {1.15f, 0.92f, 1.15f, 0.92f, 0.00f, 0.0f, 0.00f, 0.08f, 0.08f, 0.0f, -2.f, 0.70f, 0, 0, 6, 0, 0, 255},    // surprised
  {1.00f, 1.00f, 1.00f, 1.00f, 0.42f, 0.0f, 0.42f, 0.10f, 0.10f, -10.f, 2.f, 1.00f, 14, 0, 0, -4, 0, 255},  // bored
  {1.00f, 1.00f, 1.00f, 1.00f, 0.00f, 0.0f, 0.00f, 0.16f, 0.16f, 0.0f, 0.0f, 1.00f, 18, 5, 0, 0, 1, 255},   // love
  {0.90f, 1.00f, 0.90f, 1.00f, 0.25f, 8.0f, 0.25f, 0.16f, 0.16f, 10.f, -8.f, 0.95f, 10, 0, 0, 6, 0, 255},   // thinking
  {0.92f, 1.00f, 0.92f, 1.00f, 0.30f, -14.f, 0.30f, 0.16f, 0.16f, 0.0f, 6.0f, 1.00f, 16, -4, 0, 0, 0, 220}, // sad
  {0.50f, 1.04f, 1.00f, 1.00f, 0.00f, 0.0f, 0.00f, 0.50f, 0.16f, 4.0f, 0.0f, 1.00f, 16, 5, 0, 4, 0, 255},   // wink
  {0.80f, 1.00f, 0.80f, 1.00f, 0.35f, 12.f, 0.35f, 0.18f, 0.18f, -8.f, 0.0f, 1.00f, 14, 3, 0, 6, 0, 255},   // mischievous
};
static const char *const k_bx_name[BX_COUNT] = {"IDLE", "HAPPY", "SLEEPY", "CURIOUS", "SURPRISED", "BORED",
                                                "LOVE", "THINKING", "SAD", "WINK", "MISCHIEVOUS"};
const char *buddy_expr_name(uint8_t bx) { return bx < BX_COUNT ? k_bx_name[bx] : "?"; }

BuddyRig g_rig;  // one rig: F2, the Buddy views and Oracle are never on screen together

// ------------------------------------------------------------ helpers --
static uint32_t rnd(BuddyRig *r) { r->rng = r->rng * 1664525u + 1013904223u; return r->rng >> 8; }
static uint32_t rnd_in(BuddyRig *r, uint32_t lo, uint32_t hi) { return lo + rnd(r) % (hi - lo + 1); }
static float rnd_f(BuddyRig *r, float lo, float hi) { return lo + (hi - lo) * (float)(rnd(r) & 0xFFFF) / 65535.0f; }
static float clamp01(float u) { return u < 0 ? 0 : (u > 1 ? 1 : u); }
static float e_out3(float u) { u = 1 - clamp01(u); return 1 - u * u * u; }
static float e_io3(float u) { u = clamp01(u); return u < 0.5f ? 4 * u * u * u : 1 - powf(-2 * u + 2, 3) / 2; }
static float q(float v, float step) { return floorf(v / step + 0.5f) * step; }
static void lerp_p(BuddyParams *o, const BuddyParams &a, const BuddyParams &b, float u) {
  const float *pa = &a.eh, *pb = &b.eh;
  float *po = &o->eh;
  for (int i = 0; i < (int)(sizeof(BuddyParams) / sizeof(float)); i++) po[i] = pa[i] + (pb[i] - pa[i]) * u;
}
static bool blinkable(uint8_t e) { return e != BX_HAPPY && e != BX_WINK && e != BX_LOVE; }

// ------------------------------------------------------------- picker --
static const uint8_t k_pick[10] = {BX_HAPPY, BX_CURIOUS, BX_WINK, BX_LOVE, BX_THINKING,
                                   BX_BORED, BX_MISCHIEF, BX_SURPRISED, BX_SLEEPY, BX_SAD};
static const uint8_t k_w[10][3] = {{20, 12, 8}, {15, 10, 5}, {10, 6, 4}, {8, 6, 4}, {10, 8, 4},
                                   {10, 12, 12}, {10, 8, 4}, {7, 5, 3}, {5, 25, 50}, {5, 3, 3}};
static const uint16_t k_dwell[10][2] = {{3000, 5000}, {2500, 4000}, {900, 1200}, {2500, 4000}, {3000, 5000},
                                        {4000, 7000}, {2500, 4000}, {1200, 1800}, {6000, 10000}, {3000, 5000}};
static int tod(void) {  // 0 day 06-21, 1 eve 21-23, 2 night 23-06
  if (!g_ui.tv) return 0;
  const int h = g_ui.lt.tm_hour;
  return (h >= 6 && h < 21) ? 0 : (h >= 21 && h < 23) ? 1 : 2;
}
static uint8_t pick(BuddyRig *r, uint32_t *dwell) {
  const int t = tod();
  const WxData &w = g_ui.d.wx;
  int cnt[BX_COUNT] = {0};
  bool dup = false;
  for (int i = 0; i < 4; i++)
    if (r->hist[i] < BX_COUNT && ++cnt[r->hist[i]] > 1) dup = true;
  uint32_t wt[10], tot = 0;
  for (int i = 0; i < 10; i++) {
    const uint8_t e = k_pick[i];
    uint32_t v = k_w[i][t];
    if (e == BX_SURPRISED && w.valid && w.code >= 95) v += 10;
    if (e == BX_HAPPY && w.valid && w.code <= 1 && w.is_day) v += 10;
    if (e == r->hist[0] || (cnt[e] && dup) || cnt[e] > 1) v = 0;  // no immediate repeat, <= 1 repeat in the last 4
    wt[i] = v;
    tot += v;
  }
  int k = 0;
  if (tot) {
    uint32_t x = rnd(r) % tot;
    for (k = 0; k < 9 && x >= wt[k]; k++) x -= wt[k];
  }
  const uint8_t e = k_pick[k];
  uint32_t lo = k_dwell[k][0], hi = k_dwell[k][1];
  if (e == BX_SLEEPY && t == 2) { lo = 20000; hi = 40000; }
  *dwell = rnd_in(r, lo, hi);
  memmove(r->hist + 1, r->hist, 3);
  r->hist[0] = e;
  return e;
}

// ------------------------------------------------------------ the rig --
void buddy_rig_init(BuddyRig *r, uint8_t mode, uint32_t now) {
  const uint32_t seed = r->rng ? r->rng : (mode == BRM_F2 ? 0xF2B0DDu : 0xB0DD1E5u);
  memset(r, 0, sizeof(*r));
  r->rng = seed;
  r->mode = mode;
  r->expr = r->prev = BX_IDLE;
  memset(r->hist, 0xFF, sizeof(r->hist));
  r->from = r->fr.p = k_bx[BX_IDLE];
  r->m_t0 = now - 1000;
  r->m_dur = 240;
  r->expr_t0 = r->out_t0 = now;
  r->next_pick = now + rnd_in(r, 6000, 15000);
  r->next_blink = now + rnd_in(r, 1200, 4000);
  r->blink_t0 = now - 10000;
  r->sacc_t0 = now - 1000;
  r->sacc_hold = rnd_in(r, 400, 2000);
  r->next_look = now + rnd_in(r, 6000, 14000);
  r->look_t0 = now - 100000;
  r->next_twitch = now + rnd_in(r, 4000, 9000);
  r->twitch_t0 = now - 10000;
  r->glance_t0 = now - 10000;
  r->react_t0 = now - 10000;
  r->s = r->s > 0 ? r->s : 1.0f;
  for (int i = 0; i < 4; i++) r->bb_old[i] = r->bb_new[i] = r->dirty[i] = 0;
}
// v3.1.5 pop clamp, from geometry: the largest pop scale (<= BUDDY_POP_SPEC 1.10) that keeps the face top
// (eye + halo of the tallest expression, at the worst look-up + breathing offset) at y >= BUDDY_FACE_TOP_MIN.
//   unit = 44 x max eye height (k_bx) + 3 halo           px per unit scale at s = 1 (hearts, WOOZY, K.O. are lower)
//   up   = ceil((3 look-around + 1.5 breathing) x A)     whole px at s = 1 (A = 0.75 on F2, 1 elsewhere)
//   top(k) = cy - up s - 0.5 s (breathing eye height) - 1.5 (int truncation + halo rounding) - unit s k
float buddy_pop_max(uint8_t mode, float cy, float s, float *top_at_max) {
  float ehmax = 0;
  for (int i = 0; i < BX_COUNT; i++) {
    if (k_bx[i].eh > ehmax) ehmax = k_bx[i].eh;
    if (k_bx[i].eh_r > ehmax) ehmax = k_bx[i].eh_r;
  }
  float unit = 44.0f * ehmax + 3.0f;
  const float heart = 1.35f * (96.0f * 0.8f / 3.0f) * 1.08f;  // heart top above its centre, max pulse
  if (heart > unit) unit = heart;
  if (37.0f > unit) unit = 37.0f;  // WOOZY spiral r34 + stroke
  const float A = mode == BRM_F2 ? 0.75f : 1.0f;
  const float up = ceilf((3.0f + 1.5f) * A);
  const float base = cy - up * s - 0.5f * s - 1.5f;
  float k = (base - BUDDY_FACE_TOP_MIN) / (unit * s);
  if (k > BUDDY_POP_SPEC) k = BUDDY_POP_SPEC;
  if (k < 1.0f) k = 1.0f;
  if (top_at_max) *top_at_max = base - unit * s * k;
  return k;
}
void buddy_place(BuddyRig *r, float cx, float cy, float s) {
  if (r->pop_max < 1.0f || cy != r->cy || s != r->s) r->pop_max = buddy_pop_max(r->mode, cy, s, nullptr);  // once per placement
  r->cx = cx; r->cy = cy; r->s = s;
}

static void set_expr(BuddyRig *r, uint8_t e, uint32_t now, bool snap) {
  if (e == r->expr && !snap) return;
  r->from = r->fr.p;
  r->m_t0 = now;
  r->m_dur = snap ? 0 : 240;
  if (e != r->expr) {
    r->prev = r->expr;
    r->out_t0 = r->expr_t0;
    r->expr = e;
    r->expr_t0 = now;
  }
}

static uint8_t resolve(BuddyRig *r, uint32_t now) {
  // Priority: REACT > THINK > OFFLINE > EXPR > IDLE (browse and pokes are app-only EXPR overrides)
  if ((int32_t)(r->react_end - now) > 0) { r->state = BS_REACT; return r->react_expr; }
  if (r->special && (int32_t)(r->poke_end - now) <= 0) r->special = BSP_NONE;
  if (r->think) { r->state = BS_THINK; return BX_THINKING; }
  r->offline = g_ui.d.net.wifi_state == WIFI_ST_BACKOFF;
  if (r->offline) { r->state = BS_OFFLINE; return BX_SAD; }
  if (r->browse && (int32_t)(r->browse_end - now) > 0) { r->state = BS_EXPR; return r->browse; }
  r->browse = 0;
  if ((int32_t)(r->poke_end - now) > 0) {  // poke / press / Oracle outcome (WOOZY, K.O. draw their own eyes)
    r->state = BS_EXPR;
    r->dwell_end = r->poke_end;
    return r->special ? (uint8_t)BX_IDLE : r->react_expr;
  }
  if (r->state == BS_EXPR && (int32_t)(r->dwell_end - now) > 0 && r->expr != BX_IDLE) return r->expr;
  if (r->state != BS_IDLE) {  // dwell over (or a react / think / offline just ended): back to IDLE
    r->state = BS_IDLE;
    if ((int32_t)(r->next_pick - now) < 6000) r->next_pick = now + rnd_in(r, 6000, 15000);
  }
  if ((int32_t)(now - r->next_pick) >= 0) {
    uint32_t dw;
    const uint8_t e = pick(r, &dw);
    r->dwell_end = now + dw;
    r->next_pick = r->dwell_end + rnd_in(r, 6000, 15000);
    r->state = BS_EXPR;
    return e;
  }
  return BX_IDLE;
}

// Canonical accent phase: the accent drawing of expression e depends on t only through this
// value, so a frame whose accent looks the same also hashes the same (0 ms when unchanged).
static uint16_t acc_phase(uint8_t e, uint8_t special, uint32_t t) {
  if (special) return (uint16_t)(t % 60000u / 40u * 40u);
  t = t / 40u * 40u;
  switch (e) {
    case BX_LOVE: return (uint16_t)(t % 1400u);
    case BX_SLEEPY: return (uint16_t)(t % 2000u);
    case BX_CURIOUS: return (uint16_t)(t % 1000u);
    case BX_SURPRISED: return (uint16_t)(t > 600u ? 600u : t);
    case BX_THINKING: return (uint16_t)((t / 500u) % 4u * 500u);
    case BX_SAD: { const uint32_t u = t % 4000u; return (uint16_t)(u < 1000u ? u : 1000u); }
    case BX_WINK: return (uint16_t)(t < 300u ? t : 300u);
    default: return 0;  // IDLE, happy, bored, mischievous: static accents (or none)
  }
}

void buddy_tick(BuddyRig *r, uint32_t now) {
  const float A = r->mode == BRM_F2 ? 0.75f : 1.0f;  // F2 motion amplitudes (spec: +-9/6, +-5/2, 1.0 px)
  const uint8_t e = resolve(r, now);
  const bool react = r->state == BS_REACT;
  set_expr(r, e, now, react && r->snap);  // REACT pop snaps the params (spec §4)
  r->snap = 0;
  // morph
  const float mu = r->m_dur ? e_io3((float)(now - r->m_t0) / r->m_dur) : 1.0f;
  BuddyFrame &f = r->fr;
  BuddyParams cur;
  lerp_p(&cur, r->from, k_bx[r->expr], mu);  // target is always the table row
  const uint32_t te = now - r->expr_t0;
  // expression motion overrides (spec §3)
  if (r->expr == BX_THINKING) { cur.ix -= ((now / 700) & 1) ? 6.0f * mu : 0; cur.m_x += ((now / 250) & 1) ? 1.0f : -1.0f; }
  if (r->expr == BX_BORED) cur.ix += 2.0f * sinf(now / 1000.0f);
  if (r->expr == BX_SLEEPY && (te % 8000) >= 7100) cur.m_open = 4;
  if (r->speak) cur.m_open = ((now / 83) & 1) ? 3.0f : 0.0f;
  // mouth twitch (idle smile): +2 w, +1 curve for 200 ms every 4-9 s
  if ((int32_t)(now - r->next_twitch) >= 0) { r->twitch_t0 = now; r->next_twitch = now + rnd_in(r, 4000, 9000); }
  const uint32_t tw = now - r->twitch_t0;
  if (tw < 200) { const float b = sinf(3.14159f * tw / 200.0f); cur.m_w += 2 * b; cur.m_curve += 1 * b; }
  // blink: 60 ms close (ease-in quad), 30 ms hold, 90 ms open (ease-out cubic); next 2.5-6 s; 15 % double
  if (!react && blinkable(r->expr) && !r->special) {
    if ((int32_t)(now - r->next_blink) >= 0) {
      r->blink_t0 = now;
      r->next_blink = now + rnd_in(r, 2500, 6000);
      r->dbl_at = (rnd(r) % 100) < 15 ? now + 180 + 120 : 0;
    }
    if (r->dbl_at && (int32_t)(now - r->dbl_at) >= 0) { r->blink_t0 = now; r->dbl_at = 0; }
  } else {
    r->blink_t0 = now - 10000;
  }
  const uint32_t bt = now - r->blink_t0;
  float blink = 0;
  if (bt < 60) blink = (bt / 60.0f) * (bt / 60.0f);
  else if (bt < 90) blink = 1;
  else if (bt < 180) blink = 1 - e_out3((bt - 90) / 90.0f);
  // saccade: target +-12 / +-8 (30 % centre), 80 ms ease-out cubic, hold 0.4-2 s
  if ((int32_t)(now - (r->sacc_t0 + 80 + r->sacc_hold)) >= 0) {
    const float u = e_out3((now - r->sacc_t0) / 80.0f);
    r->sx0 = r->sx0 + (r->sx1 - r->sx0) * u;
    r->sy0 = r->sy0 + (r->sy1 - r->sy0) * u;
    if (rnd(r) % 100 < 30) { r->sx1 = 0; r->sy1 = 0; }
    else { r->sx1 = rnd_f(r, -12, 12); r->sy1 = rnd_f(r, -8, 8); }
    r->sacc_t0 = now;
    r->sacc_hold = rnd_in(r, 400, 2000);
  }
  const float su = e_out3((now - r->sacc_t0) / 80.0f);
  const float sk = r->expr == BX_IDLE ? 1.0f : 0.4f;
  float gx = (r->sx0 + (r->sx1 - r->sx0) * su) * sk, gy = (r->sy0 + (r->sy1 - r->sy0) * su) * sk;
  // look-around: every 6-14 s, +-8 / +-3 px over 250 ms, hold 0.8-1.5 s, back; the iris leads (saccade)
  if ((int32_t)(now - r->next_look) >= 0) {
    r->look_t0 = now;
    r->look_hold = rnd_in(r, 800, 1500);
    r->lx = (rnd(r) & 1) ? 8.0f : -8.0f;
    r->ly = rnd_f(r, -3, 3);
    r->next_look = now + r->look_hold + 500 + rnd_in(r, 6000, 14000);
    r->sx0 = gx / sk; r->sy0 = gy / sk; r->sx1 = r->lx * 1.2f; r->sy1 = r->ly; r->sacc_t0 = now; r->sacc_hold = r->look_hold;
  }
  const uint32_t lt = now - r->look_t0;
  float lk = 0;
  if (lt < 250) lk = e_io3(lt / 250.0f);
  else if (lt < 250 + r->look_hold) lk = 1;
  else if (lt < 500 + r->look_hold) lk = 1 - e_io3((lt - 250 - r->look_hold) / 250.0f);
  float dx = r->lx * lk, dy = r->ly * lk;
  // encoder glance: iris +-12 px for 120 ms, back in 300 ms
  const uint32_t gt = now - r->glance_t0;
  if (gt < 120) gx += r->glance_dir * 12 * e_out3(gt / 120.0f);
  else if (gt < 420) gx += r->glance_dir * 12 * (1 - e_io3((gt - 120) / 300.0f));
  // breathing bob 1.5 px, 3.2 s; eye height x (1 + 0.015 sin) -> whole px
  const float br = sinf(6.2831853f * (now % 3200) / 3200.0f);
  dy += 1.5f * br;
  if (r->expr == BX_BORED) { const uint32_t ph = te % 5000; if (ph < 600) dy += 3 * sinf(3.14159f * ph / 600.0f); }
  // REACT pop: 1.00 -> 1.10 in 40 ms, back in 160 ms ease-out
  const uint32_t rt = now - r->react_t0;
  // v3.1.5: peak = this screen's geometric clamp (buddy_pop_max, computed in buddy_place)
  const float pk = (r->pop_max >= 1.0f ? r->pop_max : 1.0f) - 1.0f;
  float sc = 1;
  if (rt < 40) sc = 1 + pk * rt / 40.0f;
  else if (rt < 200) sc = 1 + pk * (1 - e_out3((rt - 40) / 160.0f));
  // ---- frame snapshot (quantised so a held pose repeats bit-for-bit)
  memset(&f, 0, sizeof(f));
  f.p = cur;
  float *pp = &f.p.eh;
  for (int i = 0; i < (int)(sizeof(BuddyParams) / sizeof(float)); i++) pp[i] = q(pp[i], i < 9 || i == 11 || i == 16 ? 1 / 64.0f : 0.5f);
  f.p.eye_a = q(cur.eye_a, 1);
  f.dx = q(dx * A, 1);
  f.dy = q(dy * A, 1);
  f.scale = q(sc, 1 / 128.0f);
  if (f.scale > 1 + pk) f.scale = 1 + pk;  // quantising must not round past the clamp
  f.blink = q(blink, 1 / 16.0f);
  f.gx = q(gx * A, 1);
  f.gy = q(gy * A, 1);
  f.bh = (int8_t)q(88 * 0.015f * br * A, 1);
  f.expr = r->expr;
  f.out_expr = r->prev;
  f.special = r->special;
  f.acc_in = q(clamp01((mu - 0.5f) / 0.5f), 1 / 8.0f);
  f.acc_out = q(1 - clamp01((now - r->m_t0) / 120.0f), 1 / 8.0f);
  if (r->m_dur == 0) { f.acc_in = 1; f.acc_out = 0; }
  f.heart_k = r->expr == BX_LOVE ? q(1.04f + 0.04f * sinf(6.2831853f * (now % 1200) / 1200.0f), 1 / 64.0f) : 1.0f;
  f.at = acc_phase(r->expr, r->special, te);
  f.out_at = acc_phase(r->prev, 0, now - r->out_t0);
  f.dim = (r->mode == BRM_APP && g_ui.scr == SCR_BUDDY && g_ui.prefs.night_dim && g_ui.night) ? 120 : 0;
  if (f.acc_out <= 0) { f.out_expr = BX_IDLE; f.out_at = 0; }
  // key + dirty rect (union of the last and this frame's bounds)
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)&f;
  for (size_t i = 0; i < sizeof(f); i++) h = (h ^ b[i]) * 16777619u;
  r->fkey = h | 1u;
  memcpy(r->bb_old, r->bb_new, sizeof(r->bb_old));
  buddy_bounds(r, r->cx, r->cy, r->s, r->bb_new);
  r->dirty[0] = r->bb_old[0] < r->bb_new[0] ? r->bb_old[0] : r->bb_new[0];
  r->dirty[1] = r->bb_old[1] < r->bb_new[1] ? r->bb_old[1] : r->bb_new[1];
  r->dirty[2] = r->bb_old[2] > r->bb_new[2] ? r->bb_old[2] : r->bb_new[2];
  r->dirty[3] = r->bb_old[3] > r->bb_new[3] ? r->bb_old[3] : r->bb_new[3];
  if (r->bb_old[2] <= r->bb_old[0]) memcpy(r->dirty, r->bb_new, sizeof(r->dirty));
}

void buddy_input(BuddyRig *r, uint8_t in_type, int d, uint32_t now) {
  if (in_type == BI_ROT || in_type == BI_SWIPE) {
    r->glance_dir = (int8_t)(d > 0 ? 1 : -1);
    r->glance_t0 = now;
    if (in_type == BI_ROT && r->mode == BRM_APP) {  // manual browse of the 10 expressions, 8 s
      int e = r->browse ? r->browse : (r->expr ? r->expr : 0);
      e = ((e - 1 + (d > 0 ? 1 : -1)) % 10 + 10) % 10 + 1;
      r->browse = (uint8_t)e;
      r->browse_end = now + 8000;
      r->special = BSP_NONE;
    }
    return;
  }
  r->browse = 0;
  r->react_t0 = now;
  r->react_end = now + 200;
  r->snap = 1;
  if (r->mode == BRM_F2) {
    r->react_expr = in_type == BI_TAP ? BX_SURPRISED : BX_HAPPY;
    return;
  }
  if (in_type == BI_PRESS) {  // happy pop, 1.5 s
    r->react_expr = BX_HAPPY;
    r->poke_end = now + 1500;
    r->special = BSP_NONE;
    return;
  }
  // poke: random of 5 for 3 s; 5 pokes in 900 ms -> WOOZY 6 s; 10 -> K.O. 8 s
  if (now - r->last_poke < 900) r->pokes++; else r->pokes = 1;
  r->last_poke = now;
  static const uint8_t k_poke[5] = {BX_SURPRISED, BX_HAPPY, BX_WINK, BX_LOVE, BX_MISCHIEF};
  if (r->pokes >= 10) { r->special = BSP_KO; r->poke_end = now + 8000; r->pokes = 0; r->react_expr = BX_SURPRISED; }
  else if (r->pokes >= 5) { r->special = BSP_WOOZY; r->poke_end = now + 6000; r->react_expr = BX_SURPRISED; }
  else { r->special = BSP_NONE; r->react_expr = k_poke[rnd(r) % 5]; r->poke_end = now + 3000; }
}
void buddy_think_begin(BuddyRig *r, uint32_t now) { (void)now; r->think = 1; r->speak = 0; }
void buddy_think_end(BuddyRig *r, uint32_t now, uint8_t o) {
  r->think = 0;
  r->speak = 0;
  r->react_t0 = now - 1000;  // no pop, just a timed expression
  r->react_expr = o == BO_OK ? BX_HAPPY : o == BO_ERR ? BX_SAD : BX_BORED;
  r->poke_end = now + (o == BO_OK ? 1200 : o == BO_ERR ? 1500 : 2000);
  r->special = BSP_NONE;
}
void buddy_speak(BuddyRig *r, bool on) { r->speak = on ? 1 : 0; }

const char *buddy_caption(const BuddyRig *r) {
  if (r->special == BSP_WOOZY) return "WOOZY!";
  if (r->special == BSP_KO) return "K.O. " AF_ELL "OUCH";
  const int hr = g_ui.tv ? g_ui.lt.tm_hour : 12;
  switch (r->expr) {
    case BX_HAPPY: return (hr >= 6 && hr < 11) ? "GOOD MORNING" : "FEELING SUNNY";
    case BX_SLEEPY: return tod() == 2 ? "ZZZ" AF_ELL : "GETTING SLEEPY";
    case BX_CURIOUS: return "OH?";
    case BX_SURPRISED: return "WHOA?!";
    case BX_BORED: return "HMPH.";
    case BX_LOVE: return "LOVE IT!";
    case BX_THINKING: return "HMM" AF_ELL;
    case BX_SAD: return r->offline ? "CAN'T REACH THE INTERNET" : "OOF" AF_ELL;
    case BX_WINK: return "HEHE";
    case BX_MISCHIEF: return "HEH.";
    default: return "ALL GOOD";
  }
}

// ------------------------------------------------------------- render --
static void heart(float cx, float cy, float s, uint16_t c) {  // callers pre-blend c over the local bg
  gfx_circle(cx - s * 0.55f, cy - s * 0.35f, s, c);
  gfx_circle(cx + s * 0.55f, cy - s * 0.35f, s, c);
  gfx_tri(cx - s * 1.5f, cy - s * 0.05f, cx + s * 1.5f, cy - s * 0.05f, cx, cy + s * 1.6f, c);
}
static void star(float cx, float cy, float s, uint16_t c) {
  gfx_tri(cx, cy - s, cx - s / 3, cy, cx + s / 3, cy, c);
  gfx_tri(cx, cy + s, cx - s / 3, cy, cx + s / 3, cy, c);
  gfx_tri(cx - s, cy, cx, cy - s / 3, cx, cy + s / 3, c);
  gfx_tri(cx + s, cy, cx, cy - s / 3, cx, cy + s / 3, c);
}
static void spiral(float cx, float cy, float rmax, float rot, float th, uint16_t c) {
  float px = cx, py = cy;
  for (int i = 1; i <= 30; i++) {
    const float f = i / 30.0f, a = rot + f * 2.6f * 6.28318f, r = rmax * f;
    const float x = cx + r * cosf(a), y = cy + r * sinf(a);
    gfx_line(px, py, x, y, th, c);
    px = x; py = y;
  }
}

// S = layout scale (positions, mouth, accents); E = eye scale = S x pop. v3.1.5: the pop grows each eye about
// its own centre, so the eye centres, mouth and accents stay put.
struct RCtx { float cx, cy, S, s, E; uint16_t bg, col; bool tight; };

static void eye_body(const RCtx &C, const BuddyFrame &f, bool left) {
  const BuddyParams &p = f.p;
  const float S = C.E;                            // sizes: pop-scaled
  const float ex = C.cx + (left ? -75 : 75) * C.S;  // centre: not moved by the pop
  const float ehk = left ? p.eh : p.eh_r, ewk = left ? p.ew : p.ew_r;
  const float top = left ? p.lid_top : p.lid_top_r, bot = left ? p.lid_bot : p.lid_bot_r;
  const uint16_t col = C.col;
  if (f.special == BSP_WOOZY) { spiral(ex, C.cy, 34 * S, (left ? 1 : -1) * f.at * 0.004f, 6 * S, col); return; }
  if (f.special == BSP_KO) {
    gfx_line(ex - 24 * S, C.cy - 24 * S, ex + 24 * S, C.cy + 24 * S, 11 * S, col);
    gfx_line(ex - 24 * S, C.cy + 24 * S, ex + 24 * S, C.cy - 24 * S, 11 * S, col);
    return;
  }
  const float hk = 1 - p.heart;
  if (hk < 0.05f) return;  // fully a heart (eye_heart)
  float eh = 88 * S * ehk * hk + f.bh * C.s, ew = 96 * S * ewk * hk;
  if (eh < 2 || ew < 2) return;
  const float r0 = 26 * S * hk;
  const float rr = r0 > eh / 2 ? eh / 2 : r0;
  const float x0 = ex - ew / 2, y0 = C.cy - eh / 2;
  if (f.blink > 0.05f && f.special == BSP_NONE) {  // lids close toward a line at cy + 0.12 h (9 px high)
    const float h = eh * (1.0f - f.blink);
    const float hh = h < 9 * S ? 9 * S : h;
    const float yc = C.cy + eh * 0.12f * f.blink;
    gfx_rrect((int)x0, (int)(yc - hh / 2), (int)ew, (int)hh, (int)(hh / 2 < rr ? hh / 2 : rr), col);
    return;
  }
  const int hp = (int)(3 * S + 0.5f);
  if (hp > 0) gfx_rrect((int)x0 - hp, (int)y0 - hp, (int)ew + 2 * hp, (int)eh + 2 * hp, (int)rr + hp, blend565(C.bg, col, 48));
  gfx_rrect((int)x0, (int)y0, (int)ew, (int)eh, (int)rr, col);
  if (bot < 0.45f && p.iris > 0.05f) {  // iris + highlights, kept inside the eye
    float ir = 31 * S * p.iris;
    const float lim = (ew < eh ? ew : eh) / 2 - 2 * S;
    if (ir > lim) ir = lim;
    if (ir > 1) {
      float ox = (p.ix + f.gx) * S, oy = (p.iy + f.gy) * S + 2 * S;
      const float mx = ew / 2 - ir - 2 * S, my = eh / 2 - ir - 2 * S;
      ox = ox > mx ? mx : (ox < -mx ? -mx : ox);
      oy = oy > my ? my : (oy < -my ? -my : oy);
      if (my < 0) oy = 0;
      if (mx < 0) ox = 0;
      const float icx = ex + ox, icy = C.cy + oy;
      gfx_circle(icx, icy, ir, blend565(col, RGB565C(18, 96, 150), 70));
      const uint16_t hl = blend565(col, 0xFFFF, 150);
      const float k = ir / (31 * S);
      gfx_circle(icx - 11 * S * k, icy - 12 * S * k, 10 * S * k, hl);
      gfx_circle(icx + 12 * S * k, icy + 11 * S * k, 5 * S * k, hl);
    }
  }
  // bottom: cheek arch in bg (0.16 = the v3 look; 0.5 = ^ squint)
  {
    const float ry = eh * (0.24f + (bot - 0.16f));
    if (ry > 0.5f) gfx_ellipse(ex, y0 + eh + eh * 0.16f, ew * 0.62f, ry, C.bg);
  }
  // top lid in bg, angled (+ = inner corner low; mirrored per eye)
  if (top > 0.01f) {
    const float a = (left ? 1 : -1) * tanf(f.p.lid_ang * 0.0174533f) * (ew / 2);  // +ang: inner (toward centre) low
    const float yl = y0 + top * eh - a, yr = y0 + top * eh + a;
    const float xl = x0 - hp - 2, xr = x0 + ew + hp + 2, yt = y0 - hp - 2;
    gfx_tri(xl, yt, xr, yt, xr, yr, C.bg);
    gfx_tri(xl, yt, xr, yr, xl, yl, C.bg);
  }
}

// love: the eye shrinks away while the heart (0.8 w, pulsing) grows on top of it
static void eye(const RCtx &C, const BuddyFrame &f, bool left) {
  eye_body(C, f, left);
  const BuddyParams &p = f.p;
  if (p.heart <= 0.01f || f.special) return;
  const float ex = C.cx + (left ? -75 : 75) * C.S;
  const float hs = 96 * C.E * 0.8f / 3.0f * p.heart * f.heart_k;
  heart(ex, C.cy, hs, blend565(C.bg, C_BUDDY_HEART, (uint8_t)p.eye_a));
  if (p.heart > 0.6f) gfx_circle(ex - hs * 0.45f, C.cy - hs * 0.45f, 0.2f * hs, 0xFFFF);
}

static void mouth(const RCtx &C, const BuddyFrame &f) {
  const BuddyParams &p = f.p;
  const float S = C.S, mx = C.cx + p.m_x * S, my = C.cy + 62 * S;
  const uint16_t c = C.col;
  if (f.special == BSP_WOOZY || f.special == BSP_KO) {
    float px = mx - 14 * S, py = my;
    for (int k = 1; k <= 7; k++) {
      const float x = mx - 14 * S + k * 4 * S, y = my + ((k & 1) ? 2.5f : -2.5f) * S;
      gfx_line(px, py, x, y, 3.5f * S, c);
      px = x; py = y;
    }
    return;
  }
  if (p.m_open > 0.5f) {
    gfx_ring(mx, my, (p.m_open + 1.5f) * S, 3 * S, c);
    return;
  }
  const float w = p.m_w * S, cv = p.m_curve * S, st = (C.tight ? 4.0f : 4.0f) * S;
  if (w < 1) { gfx_circle(mx, my, st / 2, c); return; }
  const float side = f.expr == BX_MISCHIEF ? 3 * S * f.acc_in : 0;
  float px = 0, py = 0;
  for (int i = 0; i <= 6; i++) {
    const float u = i / 3.0f - 1;  // -1..1
    const float x = mx + u * w / 2;
    const float y = my - cv / 2 + cv * (1 - u * u) - (u > 0 ? side * u * u : 0);
    if (i) gfx_line(px, py, x, y, st, c);
    px = x; py = y;
  }
}

// accent position: app coordinates around the face centre (160,104); F2 pulls them in vertically
static float AX(const RCtx &C, float x) { return C.cx + (x - 160) * C.S; }
static float AY(const RCtx &C, float y) { const float d = (y - 104) * C.S; return C.cy + (C.tight && d < 0 ? d * 0.6f : d); }

static void accents(const RCtx &C, uint8_t e, float alpha, uint16_t at) {
  if (alpha <= 0.01f || e == BX_IDLE) return;
  const float S = C.S;
  const uint8_t a = (uint8_t)(alpha * 255);
  const float lx = C.cx - 75 * S, rx = C.cx + 75 * S;
  switch (e) {
    case BX_HAPPY: case BX_LOVE:
      if (!C.tight && C.s >= 0.9f) {
        gfx_ellipse(lx - 13 * S, C.cy + 52 * S, 14 * S, 6 * S, blend565(C.bg, BX_BLUSH, (uint8_t)(150 * alpha)));
        gfx_ellipse(rx + 13 * S, C.cy + 52 * S, 14 * S, 6 * S, blend565(C.bg, BX_BLUSH, (uint8_t)(150 * alpha)));
      }
      if (e == BX_LOVE)
        for (int k = 0; k < 2; k++) {
          const float ph = ((at + k * 700) % 1400) / 1400.0f;
          heart(k ? AX(C, 268) : AX(C, 52), AY(C, 124 - ph * 62), 4.5f * S, blend565(C.bg, C_BUDDY_HEART, (uint8_t)(a * (1 - ph * 0.75f))));
        }
      break;
    case BX_SLEEPY: {
      const float ph = (at % 2000) / 2000.0f;
      const uint16_t zc = blend565(C.bg, C_MUT, (uint8_t)(a * (255 - 195 * ph) / 255));
      const float zx = AX(C, 238), zy = AY(C, 52 - 18 * ph);
      gfx_line(zx, zy, zx + 9 * S, zy, 2.5f * S, zc); gfx_line(zx + 9 * S, zy, zx, zy + 7 * S, 2.5f * S, zc); gfx_line(zx, zy + 7 * S, zx + 9 * S, zy + 7 * S, 2.5f * S, zc);
      const float Zx = zx + 14 * S, Zy = zy - 14 * S;
      gfx_line(Zx, Zy, Zx + 13 * S, Zy, 3 * S, zc); gfx_line(Zx + 13 * S, Zy, Zx, Zy + 10 * S, 3 * S, zc); gfx_line(Zx, Zy + 10 * S, Zx + 13 * S, Zy + 10 * S, 3 * S, zc);
      break;
    }
    case BX_CURIOUS:
      if (C.s >= 0.6f) {
        const float y = AY(C, 44) + q(2 * sinf(6.2831853f * (at % 1000) / 1000.0f), 1);
        gfx_text_m(&AF_LABEL, (int)AX(C, 262), (int)y, "?", blend565(C.bg, C_MUT, a), GA_C);
      }
      break;
    case BX_SURPRISED: {
      const float sy = AY(C, 62) + 10 * S * (at > 600 ? 1.0f : at / 600.0f), sx = AX(C, 286);
      const uint16_t sc = blend565(C.bg, C_ACC, a);
      gfx_circle(sx, sy, 3 * S, sc);
      gfx_tri(sx - 3 * S, sy - 0.5f, sx + 3 * S, sy - 0.5f, sx, sy - 6 * S, sc);
      break;
    }
    case BX_THINKING:
      for (int k = 0; k < 3; k++) {
        static const float dx[3] = {230, 242, 254}, dy[3] = {40, 34, 28};
        const bool lit = (int)((at / 500) % 4) > k;
        gfx_circle(AX(C, dx[k]), AY(C, dy[k]), (2.0f + k) * S, blend565(C.bg, lit ? C_MUT : blend565(C.bg, C_MUT, 90), a));
      }
      break;
    case BX_SAD: {
      const uint32_t t = at % 4000;
      if (t < 1000) {
        const float ty = 30 * S * t / 1000.0f;
        gfx_circle(lx - 10 * S, C.cy + 40 * S + (C.tight ? ty * 0.4f : ty), 4 * S, blend565(C.bg, C_ACC, a));
      }
      break;
    }
    case BX_WINK:
      if (at < 300) star(AX(C, 282), AY(C, 50), 6 * S, blend565(C.bg, C_WARN, a));
      break;
    default: break;
  }
}

static void render_frame_at(const BuddyFrame &f, float cx, float cy, float s, uint16_t bg, bool tight) {
  RCtx C;
  C.s = s;
  C.S = s;
  C.E = s * (f.scale > 0 ? f.scale : 1);
  C.cx = cx + f.dx * s;
  C.cy = cy + f.dy * s;
  C.bg = bg;
  C.tight = tight;
  C.col = blend565(bg, C_BUDDY_EYE, (uint8_t)f.p.eye_a);
  if (f.dim) C.col = blend565(bg, C.col, f.dim);
  eye(C, f, true);
  eye(C, f, false);
  mouth(C, f);
  if (f.out_expr != f.expr) accents(C, f.out_expr, f.acc_out, f.out_at);
  accents(C, f.expr, f.acc_in, f.at);
  if (f.special == BSP_KO)
    for (int k = 0; k < 2; k++) {
      const float a = f.at * 0.0022f + k * 3.14f;
      star(C.cx + cosf(a) * 96 * C.S, C.cy - 64 * C.S * (tight ? 0.6f : 1) + sinf(a) * 9 * C.S, 7 * C.S, STARC);
    }
}

void buddy_render(const BuddyRig *r, float cx, float cy, float s, uint16_t bg) {
  PROF_SCOPE(PL_FACE);
  int16_t bb[4];
  buddy_bounds(r, cx, cy, s, bb);
  if (!gfx_vis(bb[1], bb[3] - bb[1])) return;
  render_frame_at(r->fr, cx, cy, s, bg, r->mode == BRM_F2);
}

void buddy_draw_expr(uint8_t bx, float cx, float cy, float s, uint16_t bg) {
  if (!gfx_vis((int)(cy - 100 * s), (int)(200 * s))) return;
  BuddyFrame f;
  memset(&f, 0, sizeof(f));
  f.p = k_bx[bx < BX_COUNT ? bx : 0];
  f.scale = 1;
  f.heart_k = 1;
  f.expr = f.out_expr = bx;
  f.acc_in = 1;
  render_frame_at(f, cx, cy, s, bg, false);
}

// Conservative pixel bounds of everything render_frame_at() may touch for this frame.
void buddy_bounds(const BuddyRig *r, float cx, float cy, float s, int16_t out[4]) {
  const BuddyFrame &f = r->fr;
  const bool tight = r->mode == BRM_F2;
  const float S = s, E = s * (f.scale > 0 ? f.scale : 1);  // layout / pop-scaled eye sizes (eye-centre pop)
  const float fcx = cx + f.dx * s, fcy = cy + f.dy * s;
  const BuddyParams &p = f.p;
  float ewm = (p.ew > p.ew_r ? p.ew : p.ew_r), ehm = (p.eh > p.eh_r ? p.eh : p.eh_r);
  if (ewm < 0.85f) ewm = 0.85f;  // heart (0.8 w x pulse) and lids
  if (ehm < 0.85f) ehm = 0.85f;
  float hx = 48 * E * ewm + 6 * E + 3, hy = 44 * E * ehm + 6 * E + 3 + fabsf((float)f.bh) * s;
  if (f.special) { hx = 48 * E + 3; hy = 48 * E + 3; }
  float x0 = fcx - 75 * S - hx, x1 = fcx + 75 * S + hx, y0 = fcy - hy, y1 = fcy + hy;
  // mouth
  const float my = fcy + 62 * S, mw = p.m_w * S / 2 + 6 * S + 2, mh = (fabsf(p.m_curve) + p.m_open + 6) * S + 3;
  if (my + mh > y1) y1 = my + mh;
  (void)mw;
  // accents (both the incoming and the fading expression)
  for (int k = 0; k < 2; k++) {
    const uint8_t e = k ? f.out_expr : f.expr;
    const float al = k ? f.acc_out : f.acc_in;
    if (al <= 0.01f && !(k == 0 && f.special)) continue;
    float ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0;
    switch (e) {
      case BX_HAPPY: case BX_LOVE: ax0 = 30; ay0 = 54; ax1 = 290; ay1 = 168; break;   // blush, mini hearts
      case BX_SLEEPY: ax0 = 230; ay0 = 14; ax1 = 270; ay1 = 66; break;
      case BX_CURIOUS: ax0 = 248; ay0 = 26; ax1 = 276; ay1 = 62; break;
      case BX_SURPRISED: ax0 = 280; ay0 = 52; ax1 = 292; ay1 = 78; break;
      case BX_THINKING: ax0 = 224; ay0 = 22; ax1 = 260; ay1 = 46; break;
      case BX_SAD: ax0 = 70; ay0 = 136; ax1 = 90; ay1 = 182; break;
      case BX_WINK: ax0 = 274; ay0 = 42; ax1 = 290; ay1 = 58; break;
      default: continue;
    }
    const float X0 = fcx + (ax0 - 160) * S - 3, X1 = fcx + (ax1 - 160) * S + 3;
    float d0 = (ay0 - 104) * S, d1 = (ay1 - 104) * S;
    if (tight) { if (d0 < 0) d0 *= 0.6f; if (d1 < 0) d1 *= 0.6f; }
    const float Y0 = fcy + d0 - 3, Y1 = fcy + d1 + 3;
    if (X0 < x0) x0 = X0;
    if (X1 > x1) x1 = X1;
    if (Y0 < y0) y0 = Y0;
    if (Y1 > y1) y1 = Y1;
  }
  if (f.special == BSP_KO) {
    const float kx = 96 * S + 8 * S, ky = fcy - 64 * S * (tight ? 0.6f : 1);
    if (fcx - kx < x0) x0 = fcx - kx;
    if (fcx + kx > x1) x1 = fcx + kx;
    if (ky - 18 * S < y0) y0 = ky - 18 * S;
  }
  if (f.special == BSP_WOOZY || f.special == BSP_KO) { if (my + 8 * S > y1) y1 = my + 8 * S; }
  out[0] = (int16_t)(x0 < 0 ? 0 : x0 > GFX_W ? GFX_W : x0);
  out[1] = (int16_t)(y0 < 0 ? 0 : y0 > GFX_H ? GFX_H : y0);
  out[2] = (int16_t)(x1 + 1 > GFX_W ? GFX_W : (x1 + 1 < 0 ? 0 : x1 + 1));
  out[3] = (int16_t)(y1 + 1 > GFX_H ? GFX_H : (y1 + 1 < 0 ? 0 : y1 + 1));
}

uint32_t buddy_band_key(const BuddyRig *r, int band, BandWin *w, uint32_t base) {
  const int y0 = band * 20, y1 = y0 + 20;  // 12 bands of 20 rows (ui_task static_assert)
  if (r->dirty[2] > r->dirty[0] && r->dirty[3] > y0 && r->dirty[1] < y1) {
    w->x0 = r->dirty[0];
    w->x1 = r->dirty[2];
    w->key = ui_hash(r->fkey, (uint32_t)(r->dirty[0] | (r->dirty[2] << 16)));
  }
  return base;
}

void buddy_debug_set(int bx, float u, uint32_t now) {
  BuddyRig *r = &g_rig;
  r->think = 0; r->speak = 0; r->special = 0; r->browse = 0; r->poke_end = now; r->react_end = now;
  r->react_t0 = now - 10000;
  if (bx < 0) { r->state = BS_IDLE; r->next_pick = now + 600000; return; }
  if (bx >= BX_COUNT) bx = 0;
  r->state = bx ? BS_EXPR : BS_IDLE;
  r->dwell_end = r->next_pick = r->next_blink = r->next_look = r->next_twitch = now + 600000;
  r->blink_t0 = r->look_t0 = r->twitch_t0 = r->glance_t0 = now - 100000;
  r->dbl_at = 0;
  r->sacc_t0 = now - 1000; r->sacc_hold = 600000; r->sx0 = r->sx1 = r->sy0 = r->sy1 = 0; r->lx = r->ly = 0;
  r->prev = BX_IDLE;
  r->expr = (uint8_t)bx;
  r->from = k_bx[BX_IDLE];
  r->m_dur = 240;
  // accent phase per expression so the capture shows it (tear mid-fall, sparkle lit, ...)
  static const uint16_t k_at[BX_COUNT] = {1000, 1000, 700, 1000, 300, 1000, 500, 1600, 500, 160, 1000};
  if (u < 0) { r->m_t0 = now - 2000; r->expr_t0 = now - k_at[bx]; }
  else { r->m_t0 = r->expr_t0 = now - (uint32_t)(u * 240); }
  r->out_t0 = r->expr_t0;
}

bool buddy_on_screen(void) {
  return g_ui.scr == SCR_BUDDY || (g_ui.scr == SCR_APP && g_ui.app == APP_BUDDY) ||
         (g_ui.scr == SCR_HOME && g_ui.prefs.face == 1);
}

// -------------------------------------------------- idle takeover / app --
static void bd_view_draw(bool app) {
  gfx_fill(C_BG);
  buddy_render(&g_rig, 160, 104, 1.0f, C_BG);
  if (gfx_vis(220, 20)) {
    char b[48];
    const BuddyRig &r = g_rig;
    if (r.browse) snprintf(b, sizeof(b), "%s - TURN TO BROWSE", k_bx_name[r.browse]);
    else if (app && (g_ui.now - g_ui.scr_t0) < 4000) snprintf(b, sizeof(b), "TAP: PET   TURN: MOODS   HOLD: BACK");
    else snprintf(b, sizeof(b), "%s", buddy_caption(&r));
    gfx_micro(160, 226, b, r.fr.dim ? blend565(0, C_DIM, 120) : C_DIM, GA_C);
  }
}
static uint32_t bd_view_key(int b, BandWin *w, bool app) {
  const BuddyRig &r = g_rig;
  uint32_t base = ui_hash(w_status_key(true), (uint32_t)(g_ui.tv ? g_ui.lt.tm_hour * 60 + g_ui.lt.tm_min : 0));
  base = ui_hash(base, r.fr.dim);
  if (b == 11) {  // caption band
    base = ui_hash(base, (uint32_t)r.browse);
    base = ui_hash(base, app && (g_ui.now - g_ui.scr_t0) < 4000 ? 1u : 0u);
    base = ui_hash_str(base, buddy_caption(&r));
  }
  return buddy_band_key(&r, b, w, base);
}
static void bd_enter(uint32_t now) {
  buddy_rig_init(&g_rig, BRM_APP, now);
  buddy_place(&g_rig, 160, 104, 1.0f);
  buddy_tick(&g_rig, now);
}

static void bscr_enter(uint32_t now) { bd_enter(now); }
static void bscr_tick(uint32_t now) { buddy_place(&g_rig, 160, 104, 1.0f); buddy_tick(&g_rig, now); }
static void bscr_input(const UiIn &in, uint32_t now) {
  if (in.type == IN_TAP || in.type == IN_LONG) { buddy_input(&g_rig, BI_TAP, 0, now); hal_lat_tag("buddy-poke"); ui_consume(); return; }
  ui_consume();
  ui_go(SCR_HOME);  // knob / swipe wakes back to home
}
static void bscr_draw(void) { bd_view_draw(false); }
static uint32_t bscr_key(int b, BandWin *w) { return bd_view_key(b, w, false); }
const Screen SCR_BUDDY_DEF = {"BUDDY", bscr_enter, nullptr, bscr_input, bscr_tick, bscr_draw, false, false, bscr_key};

void buddyapp_enter(uint32_t now) { bd_enter(now); }
void buddyapp_leave(void) { g_rig.browse = 0; }
void buddyapp_tick(uint32_t now) { buddy_place(&g_rig, 160, 104, 1.0f); buddy_tick(&g_rig, now); }
void buddyapp_input(const UiIn &in, uint32_t now) {
  switch (in.type) {
    case IN_TAP: buddy_input(&g_rig, BI_TAP, 0, now); hal_lat_tag("buddy-poke"); ui_consume(); break;
    case IN_CLICK: buddy_input(&g_rig, BI_PRESS, 0, now); hal_lat_tag("buddy-pop"); ui_consume(); break;
    case IN_ROT: buddy_input(&g_rig, BI_ROT, in.d, now); break;
    case IN_SWL: buddy_input(&g_rig, BI_SWIPE, -1, now); ui_consume(); break;
    case IN_SWR: buddy_input(&g_rig, BI_SWIPE, 1, now); break;
    default: break;
  }
}
void buddyapp_draw(void) { bd_view_draw(true); }
uint32_t buddyapp_band_key(int b, BandWin *w) { return bd_view_key(b, w, true); }

// ------------------------------------------------------- split-flap saver --
static char s_sd[5] = "----", s_so[5] = "----";
static uint32_t s_sf[4];
static void saver_enter(uint32_t now) { (void)now; memcpy(s_sd, "----", 5); memset(s_sf, 0, sizeof(s_sf)); }
static void saver_tick(uint32_t now) {
  char d[5];
  if (g_ui.tv) snprintf(d, sizeof(d), "%02d%02d", g_ui.lt.tm_hour, g_ui.lt.tm_min);
  else memcpy(d, "----", 5);
  for (int i = 0; i < 4; i++)
    if (d[i] != s_sd[i]) { s_so[i] = s_sd[i]; s_sd[i] = d[i]; s_sf[i] = (now + i * 60) | 1; }
}
static void saver_draw(void) {
  const bool dim = g_ui.prefs.night_dim && g_ui.night;
  gfx_fill(H_BG);
  static const int xs[4] = {4, 81, 167, 244};
  for (int i = 0; i < 4; i++) {
    float u = -1;
    if (s_sf[i] && g_ui.now - s_sf[i] < 500) u = (g_ui.now - s_sf[i]) / 500.0f;
    home_flap(xs[i], 40, s_sd[i], s_so[i], u, false);
  }
  if (gfx_vis(60, 50)) {
    gfx_circle(160.5f, 75, 4, dim ? blend565(H_BG, A_HOME, 120) : A_HOME);
    gfx_circle(160.5f, 101, 4, dim ? blend565(H_BG, A_HOME, 120) : A_HOME);
  }
  static const char *const wd[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  static const char *const mo[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  const uint16_t tc = dim ? blend565(H_BG, H_TXT, 110) : H_TXT, mc = dim ? blend565(H_BG, H_MUT, 110) : H_MUT;
  gfx_text_m(&AF_NUM_M, 160, 167, g_ui.tv ? wd[g_ui.lt.tm_wday % 7] : "AURA", tc, GA_C);
  if (gfx_vis(186, 16)) {
    char b[72], tt[10] = "", cond[24] = "";
    const AuraData &d = g_ui.d;
    if (d.wx.valid) {
      fmt_temp(tt, sizeof(tt), d.wx.temp_x10);
      const char *w = wx_text(d.wx.code);
      int i = 0;
      for (; w[i] && i < 23; i++) cond[i] = (w[i] >= 'a' && w[i] <= 'z') ? (char)(w[i] - 32) : w[i];
      cond[i] = 0;
    }
    if (g_ui.tv && d.wx.valid) snprintf(b, sizeof(b), "%d %s  " AF_DOT "  %s %s", g_ui.lt.tm_mday, mo[g_ui.lt.tm_mon % 12], tt, cond);
    else if (g_ui.tv) snprintf(b, sizeof(b), "%d %s", g_ui.lt.tm_mday, mo[g_ui.lt.tm_mon % 12]);
    else snprintf(b, sizeof(b), "WAITING FOR TIME");
    gfx_micro(160, 189, b, mc, GA_C);
  }
  gfx_micro(160, 224, "ANY TOUCH / KNOB WAKES", dim ? blend565(H_BG, H_DIM, 110) : blend565(H_BG, H_DIM, 200), GA_C, true);
}
const Screen SCR_SAVER_DEF = {"SAVER", saver_enter, nullptr, nullptr, saver_tick, saver_draw, false, false, nullptr};

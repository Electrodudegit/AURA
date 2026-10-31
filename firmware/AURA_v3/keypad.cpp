// keypad.cpp - 10-column QWERTY. Letter keys are 29x44 (the 320 px row cannot
// fit 10 gaps and 32 px keys). Shift, DEL, 123/ABC, SPACE and ASK stay >= 44.
// Modes: lower, one-shot shift, caps, number. Number mode keeps a few punctuation
// marks on the same page (no third symbols page). Font is ASCII Poppins: labels
// are Aa / AA / DEL / 123 / ABC / SPACE / ASK, not icon glyphs.
#include "keypad.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

enum KpRole : uint8_t { KR_CH = 0, KR_SHIFT, KR_BS, KR_MODE, KR_SPACE, KR_ASK };

struct KKey { int x, y, w, h; uint8_t role; char ch; };

static const char k_let0[] = "qwertyuiop";
static const char k_let1[] = "asdfghjkl";
static const char k_let2[] = "zxcvbnm";
static const char k_num0[] = "1234567890";
static const char k_num1[] = "-/:;()$&@";
static const char k_num2[] = ".,?!'\"+";

static void kgeom(uint8_t mode, int i, KKey *o) {
  const bool num = mode == KP_NUM;
  const char *r0 = num ? k_num0 : k_let0;
  const char *r1 = num ? k_num1 : k_let1;
  const char *r2 = num ? k_num2 : k_let2;
  const int kw = 29, gap = 2, kh = 44, y0 = 58;
  o->h = kh;
  o->ch = 0;
  o->role = KR_CH;
  if (i < 10) {
    o->x = 6 + i * (kw + gap);
    o->y = y0;
    o->w = kw;
    o->ch = r0[i];
    return;
  }
  i -= 10;
  if (i < 9) {
    o->x = 21 + i * (kw + gap);
    o->y = y0 + (kh + gap);
    o->w = kw;
    o->ch = r1[i];
    return;
  }
  i -= 9;
  if (i < 9) {
    // shift 44 + 7 letters + DEL 44, row width 307, x0 = 6
    o->y = y0 + 2 * (kh + gap);
    if (i == 0) { o->x = 6; o->w = 44; o->role = KR_SHIFT; return; }
    if (i == 8) { o->x = 269; o->w = 44; o->role = KR_BS; return; }
    o->x = 52 + (i - 1) * (kw + gap);
    o->w = kw;
    o->ch = r2[i - 1];
    return;
  }
  i -= 9;
  // 123/ABC 54 + SPACE + ASK 70
  o->y = y0 + 3 * (kh + gap);
  if (i == 0) { o->x = 6; o->w = 54; o->role = KR_MODE; return; }
  if (i == 1) { o->x = 62; o->w = 180; o->role = KR_SPACE; return; }
  o->x = 244; o->w = 70; o->role = KR_ASK;
}

static bool kp_putc(Keypad *k, char c) {
  if (!k->buf || k->len >= k->max_chars || (uint16_t)(k->len + 1) >= k->cap) return false;
  k->buf[k->len++] = c;
  k->buf[k->len] = 0;
  return true;
}

static void kp_shift_tap(Keypad *k) {
  if (k->mode == KP_NUM) { k->mode = KP_SHIFT; return; }  // back to letters, next char upper
  if (k->mode == KP_LOWER) k->mode = KP_SHIFT;
  else if (k->mode == KP_SHIFT) k->mode = KP_CAPS;
  else k->mode = KP_LOWER;
}

static void kp_press(Keypad *k, int i, uint32_t now, KpAct *act) {
  KKey g;
  kgeom(k->mode, i, &g);
  k->focus = (uint8_t)i;
  k->flash_i = (uint8_t)i;
  k->flash_until = now + 150;
  *act = KP_EDIT;
  switch (g.role) {
    case KR_SHIFT: kp_shift_tap(k); break;
    case KR_BS:
      if (k->len) { k->len--; if (k->buf) k->buf[k->len] = 0; }
      break;
    case KR_MODE: k->mode = k->mode == KP_NUM ? (uint8_t)KP_LOWER : (uint8_t)KP_NUM; break;
    case KR_SPACE: kp_putc(k, ' '); break;
    case KR_ASK: *act = KP_SUBMIT; break;
    default: {
      char c = g.ch;
      if ((k->mode == KP_SHIFT || k->mode == KP_CAPS) && c >= 'a' && c <= 'z') c = (char)(c - 32);
      kp_putc(k, c);
      if (k->mode == KP_SHIFT) k->mode = KP_LOWER;
      break;
    }
  }
}

void keypad_bind(Keypad *k, char *buf, uint16_t cap, uint16_t max_chars) {
  memset(k, 0, sizeof(*k));
  k->buf = buf;
  k->cap = cap;
  k->max_chars = max_chars;
  if (buf && cap) {
    buf[cap - 1] = 0;
    k->len = (uint16_t)strlen(buf);
    if (k->len > max_chars) { k->len = max_chars; buf[k->len] = 0; }
    if ((uint16_t)(k->len + 1) > cap) { k->len = (uint16_t)(cap - 1); buf[k->len] = 0; }
  }
}

KpAct keypad_input(Keypad *k, const UiIn &in, uint32_t now) {
  if (!k || !k->buf) return KP_NONE;
  // Status-band corner only. A 44 px tall target would cover the text field.
  if (in.type == IN_HOLD || (in.type == IN_SWR && in.x0 < UI_MIN_HIT_PX) ||
      (in.type == IN_TAP && in.x < UI_MIN_HIT_PX && in.y < 24))
    return KP_BACK;
  if (in.type == IN_ROT) {
    int f = (int)k->focus + in.d;
    f %= KP_NKEYS;
    if (f < 0) f += KP_NKEYS;
    k->focus = (uint8_t)f;
    return KP_EDIT;
  }
  int hit = -1;
  if (in.type == IN_CLICK) hit = k->focus;
  else if (in.type == IN_TAP) {
    for (int i = 0; i < KP_NKEYS; i++) {
      KKey g;
      kgeom(k->mode, i, &g);
      if (in.x >= g.x && in.x < g.x + g.w && in.y >= g.y && in.y < g.y + g.h) { hit = i; break; }
    }
    if (hit < 0) return KP_NONE;
  } else return KP_NONE;
  KpAct act = KP_NONE;
  kp_press(k, hit, now, &act);
  return act;
}

static void key_label(const Keypad *k, const KKey &g, char *b, int n) {
  b[0] = 0;
  switch (g.role) {
    case KR_SHIFT: snprintf(b, n, "%s", k->mode == KP_CAPS ? "AA" : "Aa"); break;
    case KR_BS: snprintf(b, n, "DEL"); break;
    case KR_MODE: snprintf(b, n, "%s", k->mode == KP_NUM ? "ABC" : "123"); break;
    case KR_SPACE: snprintf(b, n, "SPACE"); break;
    case KR_ASK: snprintf(b, n, "ASK"); break;
    default: {
      char c = g.ch;
      if ((k->mode == KP_SHIFT || k->mode == KP_CAPS) && c >= 'a' && c <= 'z') c = (char)(c - 32);
      b[0] = c;
      b[1] = 0;
      break;
    }
  }
}

void keypad_draw(const Keypad *k, uint32_t now) {
  if (!k || !k->buf) return;
  // Same tile language as the Buddy header: theme background + hairline, not a white field.
  gfx_rrect(8, 26, 304, 28, 6, C_BG);
  gfx_rrect_frame(8, 26, 304, 28, 6, C_LINE);
  if (gfx_vis(26, 28)) {
    const char *s = k->buf;
    int lo = 0, hi = k->len;
    while (lo < hi) {
      const int mid = (lo + hi) / 2;
      if (gfx_text_w(&AF_ROW, s + mid) > 276) lo = mid + 1;
      else hi = mid;
    }
    gfx_text_m(&AF_ROW, 16, 40, s + lo, k->len ? C_TXT : C_DIM);
    if (((now / 500) & 1) == 0) {
      const int cx = 16 + (k->len ? gfx_text_w(&AF_ROW, s + lo) : 0);
      if (cx < 300) gfx_rect(cx + 1, 32, 1, 14, C_ACC);
    }
  }
  for (int i = 0; i < KP_NKEYS; i++) {
    KKey g;
    kgeom(k->mode, i, &g);
    if (!gfx_vis(g.y, g.h)) continue;
    const bool f = i == k->focus;
    const bool flash = i == k->flash_i && (int32_t)(k->flash_until - now) > 0;
    w_card(g.x, g.y, g.w, g.h, 6, f || flash, f);  // Q03 when the encoder is on this key
    if (g.role == KR_ASK && !f) gfx_rrect_frame(g.x, g.y, g.w, g.h, 6, C_ACC);  // border only, fill stays C_TILE
    char lab[8];
    key_label(k, g, lab, sizeof(lab));
    uint16_t lc = C_TXT;
    if (g.role == KR_ASK) lc = C_ACC;
    else if (g.role == KR_SHIFT && !f && k->mode == KP_LOWER) lc = C_MUT;
    gfx_text_m(&AF_ROW, g.x + g.w / 2, g.y + g.h / 2, lab, lc, GA_C);
  }
}

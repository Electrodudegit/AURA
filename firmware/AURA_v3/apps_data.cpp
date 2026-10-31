// apps_data.cpp - Crypto (CoinGecko), News (RSS), and the key-gated Stocks,
// Cricket and Football screens.
#include "apps.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// --------------------------------------------------------------- helpers --
static void fmt_price(char *b, int n, float p) {
  if (p >= 1000.0f) {
    const long v = (long)(p + 0.5f);
    if (v >= 1000000) snprintf(b, n, "$ %ld,%03ld,%03ld", v / 1000000, (v / 1000) % 1000, v % 1000);
    else snprintf(b, n, "$ %ld,%03ld", v / 1000, v % 1000);
  } else if (p >= 100.0f) snprintf(b, n, "$ %.1f", (double)p);
  else if (p >= 1.0f) snprintf(b, n, "$ %.2f", (double)p);
  else snprintf(b, n, "$ %.3f", (double)p);
}
static void fmt_big(char *b, int n, float v) {
  if (v >= 1e9f) snprintf(b, n, "$%.1fB", (double)(v / 1e9f));
  else if (v >= 1e6f) snprintf(b, n, "$%.1fM", (double)(v / 1e6f));
  else snprintf(b, n, "$%.0f", (double)v);
}
static void chg_tag(int xr, int y, float chg) {
  char b[12];
  snprintf(b, sizeof(b), "%.1f%%", (double)fabsf(chg));
  const uint16_t c = chg >= 0 ? C_UP : C_DN;
  const int w = gfx_micro_w(b);
  gfx_micro(xr, y, b, c, GA_R);
  const float tx = (float)(xr - w - 8), ty = (float)y + 4;
  if (chg >= 0) gfx_tri(tx - 4, ty + 3, tx + 4, ty + 3, tx, ty - 3, c);
  else gfx_tri(tx - 4, ty - 3, tx + 4, ty - 3, tx, ty + 3, c);
}

// ----------------------------------------------------------------- Crypto --
// CoinGecko 'simple/price' has no history, so sparklines use on-device samples
// (one per successful fetch, up to 24 = ~2 h at the 5-min period).
#define SPARK_N 24
static float s_hist[4][SPARK_N];
static uint8_t s_hist_n = 0;
static uint32_t s_hist_ok = 0;
static void crypto_sample(void) {
  const AuraData &d = g_ui.d;
  if (!d.crypto.valid || d.job[JOB_CRYPTO].ok_ms == s_hist_ok) return;
  s_hist_ok = d.job[JOB_CRYPTO].ok_ms;
  if (s_hist_n == SPARK_N) {
    for (int c = 0; c < 4; c++) memmove(s_hist[c], s_hist[c] + 1, sizeof(float) * (SPARK_N - 1));
    s_hist_n--;
  }
  for (int c = 0; c < 4; c++) s_hist[c][s_hist_n] = d.crypto.c[c].price;
  s_hist_n++;
}
void crypto_debug_hist(void) {  // host: fake 24 samples
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < SPARK_N; i++)
      s_hist[c][i] = g_ui.d.crypto.c[c].price * (1.0f + 0.006f * sinf(i * 0.7f + c) + 0.0009f * i * (c == 2 ? -1 : 1));
  s_hist_n = SPARK_N;
  s_hist_ok = g_ui.d.job[JOB_CRYPTO].ok_ms;
}
static void spark(int x, int y, int w, int h, int coin, uint16_t c) {
  if (s_hist_n < 2) {
    gfx_hline(x, y + h / 2, w, blend565(C_TILE, c, 90));
    return;
  }
  float mn = 1e30f, mx = -1e30f;
  for (int i = 0; i < s_hist_n; i++) { mn = fminf(mn, s_hist[coin][i]); mx = fmaxf(mx, s_hist[coin][i]); }
  if (mx - mn < mx * 1e-5f) { mx += 1; mn -= 1; }
  float xy[2 * SPARK_N];
  for (int i = 0; i < s_hist_n; i++) {
    xy[2 * i] = x + i * (float)w / (s_hist_n - 1);
    xy[2 * i + 1] = y + h - (s_hist[coin][i] - mn) * h / (mx - mn);
  }
  gfx_polyline(xy, s_hist_n, 1.3f, c);
}
void crypto_enter(uint32_t) { crypto_sample(); }
void crypto_leave(void) {}
void crypto_input(const UiIn &in, uint32_t) {
  if (in.type == IN_CLICK) { hal_net_refresh(JOB_CRYPTO); ui_toast("Refreshing prices", 1200); }
}
void crypto_draw(void) {
  crypto_sample();  // idempotent (keyed on ok_ms)
  gfx_fill(C_BG);
  w_status("CRYPTO");
  const AuraData &d = g_ui.d;
  if (!w_job_gate(JOB_CRYPTO, d.crypto.valid && d.crypto.n)) return;
  const CryptoData &c = d.crypto;
  // hero card
  if (gfx_vis(30, 80)) {
    w_card(10, 30, 300, 78, 10, true);
    char b[40], up[12];
    snprintf(up, sizeof(up), "%s", c.c[0].name);
    for (char *p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    snprintf(b, sizeof(b), "%s  " AF_DOT "  %s", c.c[0].sym, up);
    gfx_micro(22, 41, b, C_MUT);
    chg_tag(298, 41, c.c[0].chg);
    fmt_price(b, sizeof(b), c.c[0].price);
    gfx_text_m(&AF_NUM_L, 22, 70, b, C_TXT);
    spark(190, 62, 108, 18, 0, C_ACC);
    char v[16];
    fmt_big(v, sizeof(v), c.c[0].vol);
    snprintf(b, sizeof(b), "24H VOL %s", v);
    gfx_micro(22, 93, b, C_DIM);
  }
  // Q07: no in-card pill (it covered the sparkline); the status-bar chip shows the age in C_WARN
  for (int i = 1; i < c.n && i < 4; i++) {
    const int y = 116 + (i - 1) * 36;
    if (!gfx_vis(y, 32)) continue;
    w_card(10, y, 300, 31, 8);
    const int cy = y + 15;
    gfx_ring(26, (float)cy + 0.5f, 8, 2, C_ACC);
    char l[2] = {c.c[i].sym[0], 0};
    gfx_micro(26, cy - 4, l, C_ACC, GA_C, true);
    gfx_micro(44, cy - 10, c.c[i].sym, C_TXT);
    char nm[12];
    snprintf(nm, sizeof(nm), "%s", c.c[i].name);
    for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    gfx_micro(44, cy + 2, nm, C_DIM, GA_L, true);
    const uint16_t sc = c.c[i].chg >= 0 ? C_UP : C_DN;
    spark(130, cy - 5, 70, 10, i, sc);
    char b[20];
    fmt_price(b, sizeof(b), c.c[i].price);
    gfx_text_m(&AF_LABEL_M, 262, cy, b, C_TXT, GA_R);
    chg_tag(302, cy - 10, c.c[i].chg);
  }
  w_footer("COINGECKO " AF_DOT " USD " AF_DOT " PRESS: REFRESH");
}

// ------------------------------------------------------------------- News --
static uint8_t s_story = 0;
static uint32_t s_story_t0 = 0;
#define STORY_MS 9000
void news_enter(uint32_t now) { s_story = 0; s_story_t0 = now; }
void news_leave(void) {}
void news_input(const UiIn &in, uint32_t now) {
  const int n = g_ui.d.news.n ? g_ui.d.news.n : 1;
  if (in.type == IN_ROT) s_story = (uint8_t)(((int)s_story + in.d + n * 8) % n);
  else if (in.type == IN_CLICK || in.type == IN_TAP || in.type == IN_SWL) s_story = (uint8_t)((s_story + 1) % n);
  else if (in.type == IN_SWR && in.x0 >= 32) { s_story = (uint8_t)((s_story + n - 1) % n); ui_consume(); }
  else return;
  s_story_t0 = now;
  if (in.type == IN_TAP || in.type == IN_SWL) ui_consume();
}
void news_draw(void) {
  gfx_fill(C_BG);
  w_status("NEWS");
  const AuraData &d = g_ui.d;
  if (!w_job_gate(JOB_NEWS, d.news.valid && d.news.n)) return;
  const NewsData &n = d.news;
  // auto-advance (stateless: derived from time since the last manual change)
  const uint32_t el = g_ui.now - s_story_t0;
  const uint8_t idx = (uint8_t)((s_story + el / STORY_MS) % n.n);
  const float prog = (el % STORY_MS) / (float)STORY_MS;
  w_card(10, 30, 300, 168, 10);
  if (gfx_vis(30, 170)) {
    gfx_rrect(10, 30, 3, 168, 1, C_ACC);
    char b[48], age[16];
    if (d.job[JOB_NEWS].ok_ms) fmt_age(age, sizeof(age), d.job[JOB_NEWS].ok_ms); else snprintf(age, sizeof(age), "--");
    snprintf(b, sizeof(b), "%s  " AF_DOT "  %s", n.src[0] ? n.src : "RSS", age);
    gfx_micro(24, 41, b, C_ACC);
    uint16_t st[6], ln[6];
    const char *t = n.title[idx];
    const int nl = gfx_wrap(&AF_TITLE, t, 272, st, ln, 6);
    const int show = nl > 4 ? 4 : nl;
    for (int i = 0; i < show; i++) {
      char line[NEWS_TITLE_MAX + 2];
      const int len = ln[i] < NEWS_TITLE_MAX ? ln[i] : NEWS_TITLE_MAX;
      memcpy(line, t + st[i], len);
      line[len] = 0;
      if (i == show - 1 && nl > show) {  // ellipsis on the last visible line
        char tail[NEWS_TITLE_MAX + 2];
        snprintf(tail, sizeof(tail), "%s", t + st[i]);
        w_text_ell(&AF_TITLE, 24, 72 + i * 25, tail, 272, C_TXT);
      } else gfx_text_m(&AF_TITLE, 24, 72 + i * 25, line, C_TXT);
    }
    gfx_rrect(24, 184, 272, 4, 2, C_TILE_HI);
    gfx_rrect(24, 184, (int)(272 * prog) + 2, 4, 2, C_ACC);
  }
  // story dots
  if (gfx_vis(204, 8)) {
    const int x0 = 160 - (n.n * 12) / 2 + 6;
    for (int i = 0; i < n.n; i++) gfx_circle((float)(x0 + i * 12), 208, i == idx ? 2.5f : 2.0f, i == idx ? C_ACC : C_DOT_IDLE);
  }
  w_footer("TURN: NEXT STORY   PRESS: NEXT");
}

// ---------------------------------------------- key-gated feeds (no API yet) --
static void keyed(const char *title, bool have, const char *what) {
  gfx_fill(C_BG);
  w_status(title, "");
  if (!have) {
    st_missing_key(what);
    w_footer("PRESS: CREDENTIALS   HOLD: BACK");
    return;
  }
  st_empty("Key saved", "THE LIVE FEED FOR THIS KEY", "ARRIVES WITH THE P4 UPDATE", g_ui.app == APP_STOCKS ? IC_STOCKS : g_ui.app == APP_CRICKET ? IC_CRICKET : IC_FOOTBALL);
}
static void keyed_input(const UiIn &in, bool have) {
  if (in.type == IN_CLICK && !have) settings_open_creds(false);
}
void stocks_enter(uint32_t) {}
void stocks_leave(void) {}
void stocks_input(const UiIn &in, uint32_t) { keyed_input(in, g_ui.d.keys.stocks); }
void stocks_draw(void) { keyed("STOCKS", g_ui.d.keys.stocks, "STOCKS"); }
void cricket_enter(uint32_t) {}
void cricket_leave(void) {}
void cricket_input(const UiIn &in, uint32_t) { keyed_input(in, g_ui.d.keys.cricket); }
void cricket_draw(void) { keyed("CRICKET", g_ui.d.keys.cricket, "CRICKET"); }
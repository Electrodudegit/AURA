// boot_screen.cpp - port of the AURA v2 bootSequence() as a time-based state
// machine (no delay): matrix rain -> aurora reveal -> wordmark rise + shimmer ->
// byline -> CONNECTING spinner -> ONLINE pulse -> time wait -> SYNCING bar
// (or OFFLINE). All v2 frame counts are kept as durations (frames * pace ms).
#include "prof.h"
#include "aurora_fx.h"
#include "ui_core.h"
#include "apps.h"
#include "version.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum BootPh : uint8_t { BP_MATRIX = 0, BP_COLLAPSE, BP_WAKE, BP_RISE, BP_SHIMMER, BP_BYLINE, BP_CONNECT, BP_PULSE,
                        BP_TIMEWAIT, BP_SYNC, BP_OFFSPIN, BP_OFFMSG, BP_TAIL, BP_DONE };
// v2 pacing: matrix 62 x 32 ms, collapse 15 x 9 ms, wake 10 x 30, rise 14 x 30, shimmer 12 x 32,
// byline 3 x 60, connect <= 15 s (spinner +14 deg / 30 ms), pulse 3 x 45, time wait <= 20 x 240,
// sync 8 steps, offline spinner 10 x 30 + message 900, tail 300.
static const uint16_t k_dur[BP_DONE] = {1984, 135, 300, 420, 384, 180, 15000, 135, 4800, 1200, 300, 900, 300};

#define MX_NC 53
#define MX_NR 30
struct MxCol { int16_t head; uint8_t len, spd, acc; };
static MxCol *s_col = nullptr;     // malloc'd during boot only
static uint8_t *s_cell = nullptr;  // MX_NC*MX_NR: (class << 7 | char) 0 = empty; class 0 body/dim via bits
static uint8_t *s_cls = nullptr;
static uint32_t s_rng = 0xC0DE5EEDu;
static uint32_t mx_rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static uint32_t s_steps = 0;

static uint8_t s_ph = BP_MATRIX;
static uint32_t s_ph_t0 = 0, s_t0 = 0, s_aur_t0 = 0, s_connect_t0 = 0;
static bool s_online = false, s_done = false, s_nocreds = false;
static uint8_t s_sync_i = 0;
static uint32_t s_sync_t = 0;
static const char *const k_sync_nm[8] = {"WEATHER", "FORECAST", "CRYPTO", "STOCKS", "AIR", "NEWS", "CRICKET", "FOOTBALL"};

bool boot_done(void) { return s_done; }

static void mx_free(void) {
  free(s_col); free(s_cell); free(s_cls);
  s_col = nullptr; s_cell = nullptr; s_cls = nullptr;
}
static void mx_step(void) {
  for (int i = 0; i < MX_NC; i++) {
    MxCol &c = s_col[i];
    c.acc += c.spd;
    while (c.acc >= 3) {
      c.acc -= 3;
      const int r = c.head;
      if (r >= 0 && r < MX_NR) { s_cell[r * MX_NC + i] = (uint8_t)(33 + mx_rnd() % 93); s_cls[r * MX_NC + i] = 3; }
      if (r - 1 >= 0 && r - 1 < MX_NR) { s_cell[(r - 1) * MX_NC + i] = (uint8_t)(33 + mx_rnd() % 93); s_cls[(r - 1) * MX_NC + i] = 2; }
      const int dr = r - (int)c.len / 2;
      if (dr >= 0 && dr < MX_NR) { s_cell[dr * MX_NC + i] = (uint8_t)(33 + mx_rnd() % 93); s_cls[dr * MX_NC + i] = 1; }
      const int tr = r - (int)c.len;
      if (tr >= 0 && tr < MX_NR) s_cls[tr * MX_NC + i] = 0;
      c.head++;
      if (c.head - (int)c.len > MX_NR) { c.head = -(int)(mx_rnd() % 12); c.len = (uint8_t)(6 + mx_rnd() % 12); c.spd = (uint8_t)(1 + mx_rnd() % 3); }
    }
  }
}

static void go_ph(uint8_t p, uint32_t now) {
  s_ph = p;
  s_ph_t0 = now;
  if (p == BP_WAKE) { mx_free(); s_aur_t0 = now; }
  if (p == BP_CONNECT) s_connect_t0 = now;
  if (p == BP_SYNC) { s_sync_i = 0; s_sync_t = now; }
  if (p == BP_DONE) {
    s_done = true;
    const bool need_creds = !hal_nvs_has_ssid();
    hal_log("[boot] done in %lu ms (%s)%s", (unsigned long)(now - s_t0), s_online ? "online" : "offline",
            need_creds ? " -> credentials" : "");
    if (need_creds) settings_open_creds(true);
    else ui_go(SCR_HOME);
  }
}

static void boot_enter(uint32_t now) {
  s_t0 = now;
  s_done = false;
  s_online = false;
  s_steps = 0;
  s_rng = 0xC0DE5EEDu;
  s_col = (MxCol *)malloc(sizeof(MxCol) * MX_NC);
  s_cell = (uint8_t *)calloc(MX_NC * MX_NR, 1);
  s_cls = (uint8_t *)calloc(MX_NC * MX_NR, 1);
  if (!s_col || !s_cell || !s_cls) {  // no memory for the pre-show: skip straight to the aurora
    hal_log("[boot] matrix alloc failed, skipping rain");
    go_ph(BP_WAKE, now);
    return;
  }
  for (int i = 0; i < MX_NC; i++) {
    s_col[i].head = (int16_t)-(int)(mx_rnd() % 28);
    s_col[i].len = (uint8_t)(6 + mx_rnd() % 12);
    s_col[i].spd = (uint8_t)(1 + mx_rnd() % 3);
    s_col[i].acc = 0;
  }
  go_ph(BP_MATRIX, now);
}
static void boot_leave(void) { mx_free(); aur_field_free(); }

static void boot_tick(uint32_t now) {
  const uint32_t el = now - s_ph_t0;
  const AuraData &d = g_ui.d;
  switch (s_ph) {
    case BP_MATRIX: {
      const uint32_t want = el / 32;
      while (s_steps < want && s_steps < 62) { mx_step(); s_steps++; }
      if (el >= k_dur[BP_MATRIX]) go_ph(BP_COLLAPSE, now);
      break;
    }
    case BP_CONNECT:
      s_nocreds = d.net.wifi_state == WIFI_ST_NO_CREDS;
      if (d.net.wifi_state == WIFI_ST_UP) { s_online = true; go_ph(BP_PULSE, now); }
      else if (s_nocreds || el >= k_dur[BP_CONNECT]) go_ph(BP_OFFSPIN, now);  // no credentials: no 15 s wait
      break;
    case BP_TIMEWAIT:
      if (g_ui.tv || el >= k_dur[BP_TIMEWAIT]) go_ph(BP_SYNC, now);
      break;
    case BP_SYNC: {
      // each step >= 150 ms; the weather step also waits (<= 3 s) for the first fetch
      const uint32_t st = now - s_sync_t;
      bool adv = st >= 150;
      if (s_sync_i == 0 && d.job[JOB_WX].st != JS_OK && d.job[JOB_WX].st != JS_FAIL && d.job[JOB_WX].st != JS_HEAP && st < 3000) adv = false;
      if (adv) {
        s_sync_i++;
        s_sync_t = now;
        if (s_sync_i >= 8) go_ph(BP_TAIL, now);
      }
      break;
    }
    case BP_TAIL: if (el >= k_dur[BP_TAIL]) go_ph(BP_DONE, now); break;
    case BP_DONE: break;
    default:
      if (el >= k_dur[s_ph]) {
        uint8_t nx = (uint8_t)(s_ph + 1);
        if (s_ph == BP_PULSE) nx = BP_TIMEWAIT;
        if (s_ph == BP_OFFMSG) nx = BP_TAIL;
        go_ph(nx, now);
      }
      break;
  }
}

// ------------------------------------------------------------------ draw --
#define AU_H 78
static void aurora_draw(float T) {
  // v3.1.1: shared cached field (aurora_fx.cpp), same ribbons as v2 (base 18+15i, len 36-7i, peak 150-28i)
  if (!gfx_vis(0, AU_H)) return;
  AurField *f = aur_field(0);
  aur_update(f, g_ui.now, T, 0, AU_H, AU_H, 1.0f);
  aur_draw(f, 0, AU_H, C_TXT, C_DIM, C_BG);
}
static void wordmark_draw(float e, float shim, bool shimmer) {
  PROF_SCOPE(PL_TEXT);
  if (!gfx_vis(106, 44)) return;
  const char *wm = FW_NAME;
  int n = (int)strlen(wm);
  if (n > 8) n = 8;
  const GFXfont *f = &AF_NUM_L;
  int lw[8], tot = 0;
  for (int i = 0; i < n; i++) { char b[2] = {wm[i], 0}; lw[i] = gfx_text_w(f, b); tot += lw[i]; }
  tot += (n - 1) * 14;
  int x = (320 - tot) / 2;
  for (int i = 0; i < n; i++) {
    float le = ui_clampf(e * (float)n - (float)i, 0.0f, 1.0f);
    le = ease_out(le);
    const int dy = (int)((1.0f - le) * 18.0f);
    uint16_t c;
    if (shimmer) {
      const uint8_t t2 = (uint8_t)((sinf(shim + i * 0.9f) * 0.5f + 0.5f) * 255.0f);
      c = blend565(blend565(A_HOME, A_SKY, t2), C_TXT, 90);
    } else c = blend565(C_BG, C_TXT, (uint8_t)(le * 255.0f));
    if (le > 0.02f) {
      char b[2] = {wm[i], 0};
      gfx_clip(0, 106, 320, 150);
      gfx_text(f, x, 140 + dy, b, c);
      gfx_noclip();
    }
    x += lw[i] + 14;
  }
}
static void spinner_draw(float a, uint16_t col) {
  for (int k = 0; k < 3; k++)
    gfx_arc(160, 190, 11, 3, a - k * 26.0f, a - k * 26.0f + 18.0f, blend565(C_BG, col, (uint8_t)(210 - k * 62)));
}
static void status_draw(const char *s, uint16_t c) { gfx_micro(GFX_W / 2, 208, s, c, GA_C); }

static void boot_draw(void) {
  const uint32_t now = g_ui.now, el = now - s_ph_t0;
  if (s_ph <= BP_COLLAPSE) {
    gfx_fill(0x0000);
    if (!s_cell) return;
    int clear_rows = 0;  // collapse: rows wiped top-down 16 px every 9 ms
    if (s_ph == BP_COLLAPSE) clear_rows = (int)((el / 9 + 1) * 2);
    const uint16_t HEADC = 0xBFF7, BODY = 0x07E8, DIMC = 0x03E0;
    const int r0 = gfx_y0() / 8, r1 = (gfx_y1() + 7) / 8;
    for (int r = r0 < clear_rows ? clear_rows : r0; r < r1 && r < MX_NR; r++)
      for (int i = 0; i < MX_NC; i++) {
        const uint8_t cl = s_cls[r * MX_NC + i];
        if (!cl) continue;
        gfx_char5(i * 6, r * 8, (char)s_cell[r * MX_NC + i], cl == 3 ? HEADC : (cl == 2 ? BODY : DIMC));
      }
    return;
  }
  gfx_fill(C_BG);
  const float T = (now - s_aur_t0) * (0.085f / 30.0f);
  aurora_draw(T);
  if (s_ph == BP_WAKE) return;
  // wordmark
  if (s_ph == BP_RISE) wordmark_draw(ui_clampf(el / 420.0f, 0, 1), 0, false);
  else if (s_ph == BP_SHIMMER) wordmark_draw(1.0f, 0.55f * (1 + el / 32), true);
  else wordmark_draw(1.0f, 0, false);
  if (s_ph < BP_BYLINE) return;
  // byline + rules
  {
    const uint8_t k = s_ph == BP_BYLINE ? (uint8_t)(el / 60 + 1 > 3 ? 3 : el / 60 + 1) : 3;
    gfx_micro(GFX_W / 2, 150, FW_BY, blend565(C_BG, C_MUT, (uint8_t)(k * 80)), GA_C);
    if (s_ph > BP_BYLINE) {
      const int bw2 = gfx_micro_w(FW_BY);
      const uint16_t lc = blend565(C_BG, A_HOME, 130);
      gfx_hline((320 - bw2) / 2 - 34, 156, 26, lc);
      gfx_hline((320 + bw2) / 2 + 8, 156, 26, lc);
      gfx_circle((320 - bw2) / 2 - 36.0f, 156.5f, 1.4f, A_HOME);
      gfx_circle((320 + bw2) / 2 + 36.0f, 156.5f, 1.4f, A_HOME);
    }
  }
  const float sa = (now - s_connect_t0) * (14.0f / 30.0f);
  switch (s_ph) {
    case BP_CONNECT:
      spinner_draw(sa, A_HOME);
      status_draw("CONNECTING", C_MUT);
      break;
    case BP_PULSE: {
      spinner_draw(sa, A_HOME);
      const int nring = (int)(el / 45) + 1;
      for (int i = 0; i < nring && i < 3; i++) {
        const int r2 = 12 + i * 5;
        gfx_ring(160, 190, (float)r2, 1, blend565(C_BG, C_UP, (uint8_t)(200 - (r2 - 12) * 14)));
      }
      status_draw("CONNECTING", C_MUT);
      break;
    }
    case BP_TIMEWAIT: status_draw("ONLINE", C_UP); break;
    case BP_SYNC: case BP_TAIL:
      if (s_online) {
        char b[24];
        const uint8_t i = s_sync_i > 7 ? 7 : s_sync_i;
        snprintf(b, sizeof(b), "SYNCING %s", k_sync_nm[i]);
        status_draw(b, C_DIM);
        if (gfx_vis(226, 4)) {
          gfx_rrect(60, 226, 200, 4, 2, C_TILE);
          const int done = (200 * (s_sync_i + (s_ph == BP_TAIL ? 0 : 1))) / 8;
          for (int px = 0; px < done && px < 200; px += 4) gfx_rect(60 + px, 226, 4, 4, blend565(A_HOME, A_SKY, (uint8_t)(px * 255 / 200)));
        }
      } else status_draw(s_nocreds ? "NO WI-FI SET UP" : "OFFLINE - WILL RETRY", C_DN);
      break;
    case BP_OFFSPIN: spinner_draw(sa, C_DN); status_draw("CONNECTING", C_MUT); break;
    case BP_OFFMSG: status_draw(s_nocreds ? "NO WI-FI SET UP" : "OFFLINE - WILL RETRY", C_DN); break;
    default: break;
  }
}

const Screen SCR_BOOT_DEF = {"BOOT", boot_enter, boot_leave, nullptr, boot_tick, boot_draw, false, false, nullptr};

// host capture helper: jump to a phase at a given elapsed time (ms into the phase)
void boot_debug_set(uint8_t ph, uint32_t el, bool online, uint8_t sync_i) {
  if (ph <= BP_COLLAPSE && s_cell) {
    while (s_steps < 62 && s_steps < (ph == BP_MATRIX ? el / 32 : 62)) { mx_step(); s_steps++; }
  }
  if (ph > BP_COLLAPSE) mx_free();
  s_ph = ph;
  s_ph_t0 = g_ui.now - el;
  if (ph >= BP_WAKE && s_aur_t0 == 0) s_aur_t0 = g_ui.now - 900;
  s_online = online;
  s_sync_i = sync_i;
  s_nocreds = false;
}

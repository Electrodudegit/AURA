// host_bands.cpp (v3.1.3; v3.1.4 Buddy v2 scenes + per-pixel window check) - band-skip soundness test + per-band compose profile.
//  ./aura_host --bands [nocache]
// For every scene it renders ~25 s of frames at 25 fps (with a minute rollover) and checks the
// ui_band_key() contract: whenever a band's non-zero key repeats, its pixels must be identical to
// the previous frame. It also reports per-band compose time (host ns) for drawn and skipped bands,
// which tools/hw_calib.py scales to ESP32 ms with the hardware-calibrated factor.
#include "apps.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
void host_frame(uint32_t now);
AuraData &host_data(void);
struct tm &host_tm(void);
extern bool g_host_nocache;
static uint16_t s_buf[320 * 20];
static uint16_t s_prev[12][320 * 20];  // last frame per band (window check)
static uint64_t now_ns(void) {
  return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static uint32_t fnv(const uint16_t *p, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void set_clock(uint32_t ms) {  // 10:41:50 + ms
  struct tm &t = host_tm();
  const uint32_t s = 10 * 3600 + 41 * 60 + 50 + ms / 1000;
  t.tm_hour = (int)(s / 3600) % 24; t.tm_min = (int)(s / 60) % 60; t.tm_sec = (int)(s % 60);
}
struct Scene { const char *name; void (*setup)(void); void (*step)(int fr, uint32_t now); };
static void sc_face(uint8_t f) { g_ui.prefs.face = f; ui_go(SCR_HOME); }
static void f1(void) { sc_face(0); }
static void f2(void) { sc_face(1); }
static void f5(void) { host_data().wx.code = 2; sc_face(2); }
static void f5r(void) { host_data().wx.code = 63; sc_face(2); }
static void f6(void) { sc_face(3); }
static void buddy(void) { ui_go(SCR_BUDDY); }
static void menu(void) { g_ui.prefs.menu_list = 0; ui_go(SCR_MENU); }
// v3.1.4 Buddy v2 scenes: the app with pokes (incl. a WOOZY and a K.O. burst), a browse of all
// 10 expressions, F2 pops + glances, and the offline (forced sad) case.
static void bapp(void) { ui_open_app(APP_BUDDY); }
static void bapp_step(int fr, uint32_t now) {
  if (fr % 75 == 10) buddy_input(&g_rig, BI_TAP, 0, now);
  if (fr % 120 == 60) buddy_input(&g_rig, BI_PRESS, 0, now);
  if (fr % 50 == 30) buddy_input(&g_rig, BI_SWIPE, (fr & 1) ? 1 : -1, now);
  if (fr >= 200 && fr < 205) buddy_input(&g_rig, BI_TAP, 0, now);  // 5 in 200 ms -> WOOZY
  if (fr >= 420 && fr < 430) buddy_input(&g_rig, BI_TAP, 0, now);  // 10 -> K.O.
}
static void browse_step(int fr, uint32_t now) { if (fr % 55 == 5) buddy_input(&g_rig, BI_ROT, 1, now); }
static void f2_step(int fr, uint32_t now) {
  if (fr % 90 == 20) buddy_input(&g_rig, (fr / 90) & 1 ? BI_TAP : BI_PRESS, 0, now);
  if (fr % 70 == 50) buddy_input(&g_rig, BI_SWIPE, 1, now);
}
static void boff(void) { host_data().net.wifi_state = WIFI_ST_BACKOFF; ui_open_app(APP_BUDDY); }
static void think_step(int fr, uint32_t now) {
  if (fr == 5) buddy_think_begin(&g_rig, now);
  if (fr == 300) buddy_think_end(&g_rig, now, BO_OK);
  if (fr == 400) buddy_think_begin(&g_rig, now);
  if (fr == 500) buddy_think_end(&g_rig, now, BO_ERR);
}
int host_bands_main(int argc, char **argv) {
  g_host_nocache = argc > 2 && strcmp(argv[2], "nocache") == 0;
  static const Scene sc[] = {{"home_F1", f1, nullptr}, {"home_F2", f2, nullptr}, {"home_F5", f5, nullptr},
                             {"home_F5_rain", f5r, nullptr}, {"home_F6", f6, nullptr}, {"buddy", buddy, nullptr},
                             {"menu_grid", menu, nullptr}, {"buddy_app", bapp, bapp_step}, {"buddy_browse", bapp, browse_step},
                             {"buddy_think", bapp, think_step}, {"buddy_offline", boff, nullptr}, {"home_F2_pops", f2, f2_step},
                             {"buddy_idle_pokes", buddy, bapp_step}};
  int bad_total = 0;
  printf("# bands %s: per band avg host ns (all frames) | drawn%% with band keys\n", g_host_nocache ? "nocache(hw-like)" : "cache");
  for (const Scene &s : sc) {
    const uint8_t code0 = host_data().wx.code, ws0 = host_data().net.wifi_state;
    uint32_t t0 = 2000000;
    set_clock(0);
    s.setup();
    for (int w = 0; w < 30; w++) { set_clock(w * 40); host_frame(t0 + w * 40); }  // past slides / count-ups
    t0 += 30 * 40;
    uint32_t pk[12] = {0}, pw[12] = {0}, ph[12] = {0}, po[12] = {0};
    bool have[12] = {false};
    double ns[12] = {0};
    int drawn[12] = {0}, bad = 0, N = 0, nwin = 0;
    double full_ns = 0, keyed_ns = 0, px_sent = 0;
    std::vector<double> fk, popk;  // keyed compose ns per frame; frames right after a Buddy pop
    for (int fr = 0; fr < 640; fr++, N++) {  // 25.6 s at 25 fps, minute rolls at +10 s
      const uint32_t now = t0 + (uint32_t)fr * 40;
      set_clock(1200 + fr * 40);
      if (s.step) s.step(fr, now);
      const bool pop = g_rig.react_t0 == now;
      const double k0 = keyed_ns;
      host_frame(now);
      g_ui.last_input = now;
      for (int b = 0; b < 12; b++) {
        BandWin w;
        const uint32_t key = ui_band_key(b, &w);
        const uint64_t a = now_ns();
        gfx_begin(s_buf, b * 20, 20);
        ui_draw();
        const uint64_t e = now_ns() - a;
        const uint32_t h = fnv(s_buf, 320 * 20);
        uint32_t ho = 2166136261u;  // hash of the columns outside the window
        for (int y = 0; y < 20; y++)
          for (int x = 0; x < 320; x++)
            if (x < w.x0 || x >= w.x1) ho = (ho ^ s_buf[y * 320 + x]) * 16777619u;
        ns[b] += (double)e;
        full_ns += (double)e;
        const bool same = key && have[b] && key == pk[b];
        const bool skip = same && w.key == pw[b];
        const bool win = same && !skip && w.x1 > w.x0;
        if (skip && h != ph[b]) {
          if (bad < 5) printf("  VIOLATION %s frame %d band %d (key repeated, pixels changed)\n", s.name, fr, b);
          bad++;
        }
        if (win) {  // per pixel: everything outside this frame's window must equal the last frame
          bool diff = false;
          for (int y = 0; y < 20 && !diff; y++)
            for (int x = 0; x < 320; x++)
              if ((x < w.x0 || x >= w.x1) && s_prev[b][y * 320 + x] != s_buf[y * 320 + x]) { diff = true; break; }
          if (diff) ho = po[b] + 1;
          else ho = po[b];
        }
        if (win && ho != po[b]) {
          if (bad < 5) printf("  VIOLATION %s frame %d band %d (pixels changed outside the window)\n", s.name, fr, b);
          bad++;
        }
        if (!skip) {
          drawn[b]++;
          if (win) {  // time the window compose like the device does
            static uint16_t wb[320 * 20];
            const uint64_t a2 = now_ns();
            gfx_begin_win(wb, w.x0, b * 20, w.x1 - w.x0, 20);
            ui_draw();
            keyed_ns += (double)(now_ns() - a2);
            px_sent += (w.x1 - w.x0) * 20;
            nwin++;
          } else { keyed_ns += (double)e; px_sent += 320 * 20; }
        }
        pk[b] = key; pw[b] = w.key; ph[b] = h; po[b] = ho; have[b] = true;
        memcpy(s_prev[b], s_buf, sizeof(s_buf));
      }
      fk.push_back(keyed_ns - k0);
      if (pop) popk.push_back(keyed_ns - k0);
    }
    printf("%-13s", s.name);
    for (int b = 0; b < 12; b++) printf(" b%d:%6.0f/%3d%%", b, ns[b] / N, drawn[b] * 100 / N);
    printf(" | full=%.0f keyed=%.0f ns/frame px_sent=%.0f/frame win_bands=%.1f/frame violations=%d\n", full_ns / N, keyed_ns / N,
           px_sent / N, nwin / (double)N, bad);
    std::sort(fk.begin(), fk.end());
    std::sort(popk.begin(), popk.end());
    printf("  %-13s keyed ns/frame median=%.0f p95=%.0f max=%.0f", s.name, fk[fk.size() / 2], fk[fk.size() * 95 / 100], fk.back());
    if (!popk.empty()) printf(" | pop frames n=%u median=%.0f max=%.0f", (unsigned)popk.size(), popk[popk.size() / 2], popk.back());
    printf("\n");
    bad_total += bad;
    host_data().wx.code = code0;
    host_data().net.wifi_state = ws0;
  }
  printf("bandtest %s\n", bad_total ? "FAIL" : "PASS");
  return bad_total ? 1 : 0;
}

// host_prof.cpp - compose-cost profiler on the box: renders N frames of a screen
// (all 12 bands) and reports the per-layer exclusive time per frame (prof.h).
#include "apps.h"
#include "prof.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <vector>
void host_frame(uint32_t now);
void host_render(void);
void menu_debug_focus(uint8_t f, bool last);
void boot_debug_set(uint8_t ph, uint32_t el, bool online, uint8_t sync_i);
static double g_scale = 0;  // host->ESP32 factor (estimate), 0 = host only
struct Res { const char *name; double total_us, max_us; double layer[PL_COUNT]; };
static std::vector<Res> s_res;
static void measure(const char *name, int frames, uint32_t t0, void (*setup)(uint32_t)) {
  uint32_t acc[PL_COUNT] = {0};
  std::vector<uint32_t> tot;
  for (int f = -3; f < 0; f++) {  // warm-up: caches (tile sprite, hero mask, aurora field) build here
    const uint32_t now = t0 + (uint32_t)(f + 3) * 40 - 120;
    if (setup) setup(now);
    host_frame(now);
    host_render();
  }
  for (int f = 0; f < frames; f++) {
    const uint32_t now = t0 + (uint32_t)f * 40;
    if (setup) setup(now);
    host_frame(now);
    prof_frame_reset();
    const uint32_t a = prof_now_us();
    host_render();
    const uint32_t b = prof_now_us();
    uint32_t l[PL_COUNT];
    prof_get(l);
    for (int i = 0; i < PL_COUNT; i++) acc[i] += l[i];
    tot.push_back(b - a);
  }
  std::sort(tot.begin(), tot.end());
  Res r;
  r.name = name;
  r.total_us = tot[tot.size() / 2];  // median frame
  r.max_us = tot.back();
  for (int i = 0; i < PL_COUNT; i++) r.layer[i] = acc[i] / (double)frames;
  s_res.push_back(r);
}
static void face(uint8_t f) { g_ui.prefs.face = f; ui_go(SCR_HOME); }
static void report(void) {
  printf("%-14s %9s %9s", "screen", "frame_us", "max_us");
  for (int i = 0; i < PL_COUNT; i++) printf(" %8s", k_prof_names[i]);
  if (g_scale > 0) printf("   est_esp32_ms est_fps");
  printf("\n");
  for (auto &r : s_res) {
    printf("%-14s %9.0f %9.0f", r.name, r.total_us, r.max_us);
    for (int i = 0; i < PL_COUNT; i++) printf(" %8.0f", r.layer[i]);
    if (g_scale > 0) printf("   %12.1f %7.1f", r.total_us * g_scale / 1000.0, 1000.0 / (r.total_us * g_scale / 1000.0 > 40 ? r.total_us * g_scale / 1000.0 : 40));
    printf("\n");
  }
}
extern bool g_host_nocache;
int host_prof_main(int argc, char **argv) {
  g_host_nocache = getenv("AURA_NOCACHE") != nullptr;  // v3.1.3: model the hardware (caches refused)
  const int N = argc > 2 ? atoi(argv[2]) : 100;
  if (argc > 3) g_scale = atof(argv[3]);
  // HOME faces (steady state, past the gauge count-up)
  static const char *nm[4] = {"home_F1", "home_F2", "home_F5", "home_F6"};
  for (int f = 0; f < 4; f++) {
    face((uint8_t)f);
    host_frame(300000 + f * 100000);
    measure(nm[f], N, 300000 + f * 100000 + 3000, nullptr);
  }
  ui_go(SCR_BUDDY);
  host_frame(700000);
  measure("buddy", N, 703000, nullptr);
  // boot: matrix and aurora/wordmark/connecting phases
  ui_go(SCR_BOOT);
  host_frame(800000);
  measure("boot_matrix", N / 4 > 10 ? N / 4 : 10, 800000, [](uint32_t) { boot_debug_set(0, 1200, false, 0); });
  measure("boot_connect", N, 801000, [](uint32_t) { boot_debug_set(6, 1200, false, 0); });
  // menu grid / list
  g_ui.prefs.menu_list = 0;
  ui_go(SCR_MENU);
  host_frame(900000);
  menu_debug_focus(APP_CRYPTO, true);
  measure("menu_grid", N, 902000, nullptr);
  g_ui.prefs.menu_list = 1;
  ui_go(SCR_MENU);
  host_frame(1000000);
  menu_debug_focus(APP_CRYPTO, true);
  measure("menu_list", N, 1002000, nullptr);
  g_ui.prefs.menu_list = 0;
  report();
  return 0;
}

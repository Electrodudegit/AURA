// host_main.cpp - capture shim: runs the device renderer + screens on the PC and
// writes 320x240 frames (PPM, converted to PNG by tools/ppm2png.py).
#include "apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern bool g_host_verbose;
void boot_debug_set(uint8_t ph, uint32_t el, bool online, uint8_t sync_i);
void buddy_debug_set(int bx, float morph_u, uint32_t now);
void menu_debug_focus(uint8_t f, bool last);
void host_app_states(void);  // host_captures.cpp

static uint16_t s_fb[320 * 240];
static uint16_t s_band[320 * 20];
static AuraData s_d;
static struct tm s_tm;
static bool s_tv = true;
static uint32_t s_now = 100000;
static const char *s_out = "../captures";

AuraData &host_data(void) { return s_d; }
struct tm &host_tm(void) { return s_tm; }
void host_set_tv(bool tv) { s_tv = tv; }
uint32_t host_now(void) { return s_now; }

void host_frame(uint32_t now) {
  s_now = now;
  g_ui.last_input = now;  // never let the idle timer fire during captures
  ui_frame_begin(now, &s_d, &s_tm, s_tv);
}
void host_render(void) {
  for (int b = 0; b < 12; b++) {
    gfx_begin(s_band, b * 20, 20);
    ui_draw();
    for (int i = 0; i < 320 * 20; i++) {
      const uint16_t v = s_band[i];
      s_fb[b * 320 * 20 + i] = (uint16_t)((v >> 8) | (v << 8));
    }
  }
}
void host_save(const char *name) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ppm", s_out, name);
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  fprintf(f, "P6\n320 240\n255\n");
  for (int i = 0; i < 320 * 240; i++) {
    const uint16_t c = s_fb[i];
    const uint8_t r = (uint8_t)(((c >> 11) & 31) * 255 / 31), g = (uint8_t)(((c >> 5) & 63) * 255 / 63), bb = (uint8_t)((c & 31) * 255 / 31);
    fputc(r, f); fputc(g, f); fputc(bb, f);
  }
  fclose(f);
  printf("capture %s\n", name);
}
void host_shot(const char *name, uint32_t now) {
  host_frame(now);
  host_render();
  host_save(name);
}

static void sample_data(void) {
  memset(&s_d, 0, sizeof(s_d));
  s_d.net.wifi_state = WIFI_ST_UP;
  s_d.net.rssi = -58;
  s_d.net.ip = 0x2A01A8C0;  // 192.168.1.42
  s_d.batt.pct = 88;
  s_d.batt.vbat_mv = 4012;
  s_d.ble.started = true;
  s_d.ble.advertising = true;
  WxData &w = s_d.wx;
  w.valid = true; w.temp_x10 = 274; w.feels_x10 = 296; w.hum = 64; w.wind_x10 = 112; w.uv_x10 = 52; w.code = 2; w.is_day = true;
  w.ndays = 5;
  const uint8_t codes[5] = {61, 63, 2, 0, 80};
  const int16_t hi[5] = {310, 280, 300, 310, 290}, lo[5] = {240, 230, 240, 245, 238};
  for (int i = 0; i < 5; i++) { w.day[i].code = codes[i]; w.day[i].hi_x10 = hi[i]; w.day[i].lo_x10 = lo[i]; w.day[i].pop = (uint8_t)(20 + i * 12); }
  w.sunrise_min = 6 * 60 + 12; w.sunset_min = 18 * 60 + 2;
  w.nh = 12; w.hour0 = 14;
  const int16_t hh[12] = {274, 279, 281, 276, 268, 259, 252, 247, 244, 242, 240, 239};
  memcpy(w.hour_x10, hh, sizeof(hh));
  s_d.air.valid = true; s_d.air.aqi = 72; s_d.air.pm25_x10 = 221; s_d.air.pm10_x10 = 386;
  CryptoData &c = s_d.crypto;
  c.valid = true; c.n = 4;
  const char *sy[4] = {"BTC", "ETH", "SOL", "XRP"}, *nm[4] = {"Bitcoin", "Ethereum", "Solana", "XRP"};
  const float pr[4] = {67412.0f, 3289.5f, 152.37f, 0.5231f}, ch[4] = {2.4f, -1.1f, 4.8f, -0.6f};
  for (int i = 0; i < 4; i++) { snprintf(c.c[i].sym, 6, "%s", sy[i]); snprintf(c.c[i].name, 10, "%s", nm[i]); c.c[i].price = pr[i]; c.c[i].chg = ch[i]; c.c[i].vol = 3.12e10f / (float)(i + 1); }
  NewsData &n = s_d.news;
  n.valid = true; n.n = 6; snprintf(n.src, sizeof(n.src), "BBC NEWS");
  const char *t[6] = {"Monsoon retreats early as northern states brace for a dry, warm November",
                      "Chip makers race to expand capacity as AI demand keeps climbing",
                      "City council approves new metro line linking the airport and downtown",
                      "Scientists map the deepest coral reef ever found in the Indian Ocean",
                      "Electric two-wheeler sales hit a record high in September",
                      "Space agency confirms the launch window for its next lunar mission"};
  for (int i = 0; i < 6; i++) snprintf(n.title[i], NEWS_TITLE_MAX, "%s", t[i]);
  StocksData &st = s_d.stocks;
  st.valid = true; st.n = 4;
  const char *ssy[4] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
  const char *snm[4] = {"Apple", "Microsoft", "Alphabet", "Amazon"};
  const float spr[4] = {189.30f, 420.15f, 162.40f, 178.05f}, sch[4] = {1.2f, -0.4f, 0.8f, -1.1f};
  for (int i = 0; i < 4; i++) {
    snprintf(st.row[i].sym, sizeof(st.row[i].sym), "%s", ssy[i]);
    snprintf(st.row[i].name, sizeof(st.row[i].name), "%s", snm[i]);
    st.row[i].price = spr[i];
    st.row[i].chg = sch[i];
  }
  CricketData &cr = s_d.cricket;
  cr.valid = true; cr.n = 2;
  snprintf(cr.m[0].name, sizeof(cr.m[0].name), "Gamma vs Delta, 2nd T20");
  snprintf(cr.m[0].team[0], sizeof(cr.m[0].team[0]), "Gamma");
  snprintf(cr.m[0].team[1], sizeof(cr.m[0].team[1]), "Delta");
  snprintf(cr.m[0].detail, sizeof(cr.m[0].detail), "Delta need 12 runs from 8 balls");
  snprintf(cr.m[0].kind, sizeof(cr.m[0].kind), "t20");
  cr.m[0].nt = 2; cr.m[0].ni = 1; cr.m[0].started = true; cr.m[0].ended = false;
  cr.m[0].inn[0].r = 160; cr.m[0].inn[0].w = 4; snprintf(cr.m[0].inn[0].o, sizeof(cr.m[0].inn[0].o), "18.2");
  snprintf(cr.m[1].name, sizeof(cr.m[1].name), "Alpha vs Beta, 1st ODI");
  snprintf(cr.m[1].team[0], sizeof(cr.m[1].team[0]), "Alpha");
  snprintf(cr.m[1].team[1], sizeof(cr.m[1].team[1]), "Beta");
  snprintf(cr.m[1].detail, sizeof(cr.m[1].detail), "Alpha won by 5 wickets");
  snprintf(cr.m[1].kind, sizeof(cr.m[1].kind), "odi");
  cr.m[1].nt = 2; cr.m[1].ni = 2; cr.m[1].started = true; cr.m[1].ended = true;
  cr.m[1].inn[0].r = 280; cr.m[1].inn[0].w = 8; snprintf(cr.m[1].inn[0].o, sizeof(cr.m[1].inn[0].o), "50");
  cr.m[1].inn[1].r = 281; cr.m[1].inn[1].w = 5; snprintf(cr.m[1].inn[1].o, sizeof(cr.m[1].inn[1].o), "48.3");
  snprintf(s_d.loc.city, sizeof(s_d.loc.city), "EXAMPLE CITY");
  s_d.loc.lat = 12.97f; s_d.loc.lon = 77.59f;
  s_d.time_valid = true;
  for (int j = 0; j < JOB_COUNT; j++) { s_d.job[j].st = JS_OK; s_d.job[j].ok_ms = 100000 - 120000 + 115000; s_d.job[j].next_ms = 100000 + 500000; }
  memset(&s_tm, 0, sizeof(s_tm));
  s_tm.tm_year = 126; s_tm.tm_mon = 9; s_tm.tm_mday = 20; s_tm.tm_wday = 2; s_tm.tm_hour = 14; s_tm.tm_min = 32; s_tm.tm_sec = 18;
}
void host_reset_data(void) { sample_data(); }

int host_prof_main(int argc, char **argv);
int host_bands_main(int argc, char **argv);
int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "--bands") == 0) { sample_data(); ui_init(1000); return host_bands_main(argc, argv); }
  if (argc > 1 && strcmp(argv[1], "--prof") == 0) { sample_data(); ui_init(1000); return host_prof_main(argc, argv); }
  if (argc > 1) s_out = argv[1];
  mkdir(s_out, 0755);
  g_host_verbose = getenv("HOST_VERBOSE") != nullptr;
  sample_data();
  ui_init(1000);
  host_app_states();
  return 0;
}

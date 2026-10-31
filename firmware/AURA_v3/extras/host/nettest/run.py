PRE_ = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define AURA_HOST 1
#include "data_store.h"
#include "config.h"
#include "net_proto.h"
enum NetErr : int32_t { NET_OK = 0, NET_E_CONNECT = -1, NET_E_WRITE = -2, NET_E_TIMEOUT = -3, NET_E_STATUS = -4,
  NET_E_PARSE = -5, NET_E_NOBODY = -6, NET_E_HEAP = -7, NET_E_TOOBIG = -8, NET_E_PREEMPT = -9, NET_E_KEY = -10, NET_E_RATE = -11 };
struct HeapWindow { uint32_t a, b, c; };
static char s_body[NET_BODY_MAX]; static size_t s_body_n = 0; static bool s_body_over = false;
static LocData s_loc = {28.61f, 77.21f, "X"};
static uint32_t millis(void) { return 1234; }
typedef bool (*BodySink)(const uint8_t *p, size_t n);
static const char *g_file;
static int32_t https_get(const char *host, const char *path, BodySink sink, uint32_t, uint16_t *status, HeapWindow *, NetData *) {
  printf("GET https://%s%s\n", host, path);
  FILE *f = fopen(g_file, "rb"); if (!f) return NET_E_CONNECT;
  static uint8_t buf[50000]; size_t n = fread(buf, 1, sizeof(buf), f); fclose(f);
  *status = 200; size_t o = 0; srand(7);
  while (o < n) { size_t k = 1 + rand() % 512; if (o + k > n) k = n - o; if (!sink(buf + o, k)) { printf("  (sink stopped at %zu/%zu)\n", o + k, n); break; } o += k; }
  return NET_OK;
}
static bool body_sink(const uint8_t *p, size_t n) {
  const size_t room = sizeof(s_body) - 1 - s_body_n;
  if (n > room) { s_body_over = true; n = room; }
  memcpy(s_body + s_body_n, p, n); s_body_n += n; s_body[s_body_n] = 0; return !s_body_over;
}
static WxData g_wx; static AirData g_air; static CryptoData g_c; static NewsData g_news;
bool ds_set_wx(const WxData *w, uint32_t) { g_wx = *w; return true; }
bool ds_set_air(const AirData *w, uint32_t) { g_air = *w; return true; }
bool ds_set_crypto(const CryptoData *w, uint32_t) { g_c = *w; return true; }
bool ds_set_news(const NewsData *w, uint32_t) { g_news = *w; return true; }
"""
POST_ = r"""int main() {
  NetData n = {}; uint16_t h; HeapWindow hw;
  g_file = "wx.json"; int r = job_wx(&n, &h, &hw); printf("wx r=%d temp=%d feels=%d hum=%u wind=%u uv=%u code=%u day=%d nh=%u hour0=%u nd=%u rise=%d set=%d\n", r, g_wx.temp_x10, g_wx.feels_x10, g_wx.hum, g_wx.wind_x10, g_wx.uv_x10, g_wx.code, g_wx.is_day, g_wx.nh, g_wx.hour0, g_wx.ndays, g_wx.sunrise_min, g_wx.sunset_min);
  for (int i = 0; i < g_wx.nh; i++) { printf(" %d", g_wx.hour_x10[i]); }
  printf("\n");
  for (int i = 0; i < g_wx.ndays; i++) printf("  day%d code=%u hi=%d lo=%d pop=%u\n", i, g_wx.day[i].code, g_wx.day[i].hi_x10, g_wx.day[i].lo_x10, g_wx.day[i].pop);
  int fails = 0;
#define CHK(c) do { if (!(c)) { printf("  FAIL %s\n", #c); fails++; } } while (0)
  CHK(r == 0 && g_wx.temp_x10 == 234 && g_wx.feels_x10 == 257 && g_wx.uv_x10 == 0);  // legacy full fixture
  // P016: slim current= (no apparent_temperature / uv_index); night -> uv 0, day -> daily uv_index_max[0]
  g_file = "wx_slim.json"; memset(&g_wx, 0, sizeof(g_wx)); r = job_wx(&n, &h, &hw);
  printf("wx_slim(night) r=%d temp=%d feels=%d uv=%u day=%d nh=%u hour0=%u nd=%u rise=%d set=%d\n", r, g_wx.temp_x10, g_wx.feels_x10, g_wx.uv_x10, g_wx.is_day, g_wx.nh, g_wx.hour0, g_wx.ndays, g_wx.sunrise_min, g_wx.sunset_min);
  CHK(r == 0 && g_wx.valid && g_wx.temp_x10 == 234 && g_wx.feels_x10 == 234 && g_wx.uv_x10 == 0 && g_wx.hum == 73);
  CHK(g_wx.nh == 12 && g_wx.hour0 == 21 && g_wx.ndays == 5 && g_wx.sunrise_min == 384 && g_wx.sunset_min == 1066);
  CHK(g_wx.day[4].code == 51 && g_wx.day[4].pop == 13 && g_wx.day[4].hi_x10 == 312);
  g_file = "wx_slim_day.json"; memset(&g_wx, 0, sizeof(g_wx)); r = job_wx(&n, &h, &hw);
  printf("wx_slim(day) r=%d feels=%d uv=%u day=%d\n", r, g_wx.feels_x10, g_wx.uv_x10, g_wx.is_day);
  CHK(r == 0 && g_wx.is_day && g_wx.uv_x10 == 53);
  g_file = "air.json"; r = job_air(&n, &h, &hw); printf("air r=%d aqi=%u pm25=%u pm10=%u\n", r, g_air.aqi, g_air.pm25_x10, g_air.pm10_x10);
  CHK(r == 0 && g_air.valid && g_air.aqi == 169 && g_air.pm25_x10 == 1113 && g_air.pm10_x10 == 3120);
  // p0p1: markets + sparkline (live 2026-10-26, 4 coins x 168 hourly points; API order btc,eth,xrp,sol)
  g_file = "cg_markets.json"; r = job_crypto(&n, &h, &hw); printf("crypto r=%d n=%u\n", r, g_c.n);
  for (int i = 0; i < g_c.n; i++) {
    printf("  %s %s %.4f chg=%.2f vol=%.3g hi=%.4g lo=%.4g ns=%u spark[%.4g..%.4g]", g_c.c[i].sym, g_c.c[i].name, g_c.c[i].price, g_c.c[i].chg, g_c.c[i].vol, g_c.c[i].hi24, g_c.c[i].lo24, g_c.c[i].ns, g_c.c[i].spark_lo, g_c.c[i].spark_hi);
    for (int k = 0; k < g_c.c[i].ns; k += 4) printf(" %u", g_c.c[i].spark[k]);
    printf("\n");
  }
  CHK(r == 0 && g_c.valid && g_c.n == 4 && !strcmp(g_c.c[0].sym, "BTC") && !strcmp(g_c.c[2].sym, "SOL") && !strcmp(g_c.c[3].name, "XRP"));
  CHK(g_c.c[0].price == 79910.0f && g_c.c[2].price > 101.2f && g_c.c[2].price < 101.3f && g_c.c[3].price > 1.28f && g_c.c[3].price < 1.30f);
  CHK(g_c.c[0].ns == CRYPTO_SPARK_N && g_c.c[3].ns == CRYPTO_SPARK_N && g_c.c[0].spark_hi > g_c.c[0].spark_lo && g_c.c[0].spark[CRYPTO_SPARK_N - 1] == 171);
  g_file = "rss.xml"; r = job_news(&n, &h, &hw); printf("news r=%d n=%u src='%s'\n", r, g_news.n, g_news.src);
  for (int i = 0; i < g_news.n; i++) printf("  [%s]\n", g_news.title[i]);
  g_file = "rss2.xml"; r = job_news(&n, &h, &hw); printf("news2 r=%d n=%u src='%s'\n", r, g_news.n, g_news.src);
  for (int i = 0; i < g_news.n; i++) { printf("  ["); for (const char *p = g_news.title[i]; *p; p++) { if ((unsigned char)*p == 0x81) printf("<ELL>"); else putchar(*p);} printf("]\n"); }
  printf(fails ? "NETTEST FAIL (%d)\n" : "NETTEST PASS\n", fails);
  return fails ? 1 : 0;
}
"""
#!/usr/bin/env python3
"""Host test of the net_task parsers (JSON scanners + RSS stream parser) against
real responses saved on 2026-10-20 (wx.json, air.json, rss.xml; cg_markets.json 2026-10-26) and an
edge-case feed (rss2.xml). Extracts the parser section of net_task.cpp verbatim,
feeds bodies in random 1..512 B chunks.   Usage: python3 run.py [path/to/AURA_v3]"""
import os, subprocess, sys
fw = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))
src = open(os.path.join(fw, "net_task.cpp")).read()
a = src.index("// ------------------------------------------------------- JSON scanning ---")
b = src.index("static WebServer *s_web = nullptr;")  # v3.1.2: the Oracle section follows the parsers
here = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(here, "_t.cpp"), "w").write(PRE_ + src[a:b] + "\n" + POST_)
subprocess.check_call(["g++", "-std=gnu++17", "-Wall", "-Wextra", "-DAURA_HOST", "-I" + fw, "_t.cpp", os.path.join(fw, "net_proto.cpp"), "-o", "_t"], cwd=here)
subprocess.check_call(["./_t"], cwd=here)

#include "tz.h"

struct TzRow {
  const char *label;
  const char *posix;
  const char *om;
  int std_min;
};

// Index 0 is the historical default (IST). Open-Meteo values are pre-encoded
// so the weather URL never needs an extra escape pass.
static const TzRow k_tz[] = {
    {"IST", "IST-5:30", "Asia%2FKolkata", 330},
    {"UTC", "UTC0", "UTC", 0},
    {"LONDON", "GMT0BST,M3.5.0/1,M10.5.0", "Europe%2FLondon", 0},
    {"NEW YORK", "EST5EDT,M3.2.0,M11.1.0", "America%2FNew_York", -300},
    {"TOKYO", "JST-9", "Asia%2FTokyo", 540},
    {"DUBAI", "GST-4", "Asia%2FDubai", 240},
};
static const uint8_t TZ_N = (uint8_t)(sizeof(k_tz) / sizeof(k_tz[0]));

static volatile uint8_t s_idx = 0;
static volatile uint32_t s_gen = 1;

uint8_t tz_count(void) { return TZ_N; }
uint8_t tz_get(void) { return s_idx < TZ_N ? s_idx : 0; }
void tz_select(uint8_t i) {
  if (i >= TZ_N) i = 0;
  s_idx = i;
  s_gen++;
}
uint32_t tz_gen(void) { return s_gen; }
const char *tz_label(uint8_t i) { return k_tz[i < TZ_N ? i : 0].label; }
const char *tz_posix(uint8_t i) { return k_tz[i < TZ_N ? i : 0].posix; }
const char *tz_om(uint8_t i) { return k_tz[i < TZ_N ? i : 0].om; }
int tz_std_min(uint8_t i) { return k_tz[i < TZ_N ? i : 0].std_min; }

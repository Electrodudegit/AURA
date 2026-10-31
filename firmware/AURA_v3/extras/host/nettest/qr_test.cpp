// qr_test.cpp - wifi_qr payload builder + encoder (synthetic values only).
#include "wifi_qr.h"
#include <stdio.h>
#include <string.h>
static int f = 0;
#define CHECK(c, m) do { if (!(c)) { f++; printf("FAIL %s\n", m); } } while (0)
int main() {
  char b[96];
  CHECK(wifi_qr_text(b, sizeof(b), "AURA-Setup-7F3A", "k7m2-q9xw") && !strcmp(b, "WIFI:T:WPA;S:AURA-Setup-7F3A;P:k7m2-q9xw;;"), "basic");
  CHECK(wifi_qr_text(b, sizeof(b), "a;b,c", "x:y\\z\"") && !strcmp(b, "WIFI:T:WPA;S:a\\;b\\,c;P:x\\:y\\\\z\\\";;"), "escape");
  char s[16];
  CHECK(!wifi_qr_text(s, sizeof(s), "AURA-Setup-7F3A", "k7m2-q9xw") && s[0] == 0, "overflow");
  uint8_t qr[WQR_BUF_LEN];
  const int side = wifi_qr_encode("AURA-Setup-FFFF", "abcd-efgh", qr);
  CHECK(side == 29, "version 3 (29 modules)");
  printf("%s (side=%d)\n", f ? "QR TESTS FAILED" : "QR TESTS PASSED", side);
  return f;
}

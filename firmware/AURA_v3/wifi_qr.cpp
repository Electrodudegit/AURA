// wifi_qr.cpp - see wifi_qr.h.
#include "wifi_qr.h"
#include "src/qrcodegen/qrcodegen.h"
#include <string.h>

static bool put_esc(char *out, size_t cap, size_t *o, const char *s) {
  for (; *s; s++) {
    const char c = *s;
    const bool esc = c == '\\' || c == ';' || c == ',' || c == ':' || c == '"';
    if (*o + (esc ? 2u : 1u) + 1u > cap) return false;
    if (esc) out[(*o)++] = '\\';
    out[(*o)++] = c;
  }
  return true;
}
static bool put_lit(char *out, size_t cap, size_t *o, const char *s) {
  const size_t l = strlen(s);
  if (*o + l + 1u > cap) return false;
  memcpy(out + *o, s, l);
  *o += l;
  return true;
}
bool wifi_qr_text(char *out, size_t cap, const char *ssid, const char *pass) {
  size_t o = 0;
  const bool ok = put_lit(out, cap, &o, "WIFI:T:WPA;S:") && put_esc(out, cap, &o, ssid) && put_lit(out, cap, &o, ";P:") &&
                  put_esc(out, cap, &o, pass) && put_lit(out, cap, &o, ";;");
  if (cap) out[ok ? o : 0] = 0;
  return ok;
}
int wifi_qr_encode(const char *ssid, const char *pass, uint8_t *qr) {
  char txt[96];
  uint8_t tmp[WQR_BUF_LEN];
  int side = 0;
  if (wifi_qr_text(txt, sizeof(txt), ssid, pass) &&
      qrcodegen_encodeText(txt, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, WQR_MAX_VER, qrcodegen_Mask_AUTO, true))
    side = qrcodegen_getSize(qr);
  // volatile wipes (a plain memset of a dead buffer may be optimised away):
  // the payload holds the session password, tmp held encoder intermediates of it
  volatile char *v = txt;
  for (size_t i = 0; i < sizeof(txt); i++) v[i] = 0;
  volatile uint8_t *vt = tmp;
  for (size_t i = 0; i < sizeof(tmp); i++) vt[i] = 0;
  return side;
}
bool wifi_qr_module(const uint8_t *qr, int x, int y) { return qrcodegen_getModule(qr, x, y); }

#include "comp_proto.h"
#include <string.h>

static bool tok_char(char c) {
  if (c >= '2' && c <= '9') return true;
  if (c < 'A' || c > 'Z') return false;
  return c != 'I' && c != 'O';
}
static bool hex_char(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

static bool parse_u100(const char **pp, int *out) {
  const char *p = *pp;
  if (*p < '0' || *p > '9') return false;
  int v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    if (v > 100) return false;
    p++;
  }
  *out = v;
  *pp = p;
  return true;
}

// Printable ASCII field [p, end), 0..COMP_NOW_FIELD_MAX chars, copied NUL-terminated.
static bool now_field(const char *p, const char *end, char *dst) {
  const long n = end - p;
  if (n < 0 || n > COMP_NOW_FIELD_MAX) return false;
  for (long i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)p[i];
    if (c < 0x20 || c > 0x7E) return false;
    dst[i] = (char)c;
  }
  dst[n] = 0;
  return true;
}
// Decimal, 1..4 digits (no sign). Bounds are checked by the caller.
static bool parse_dec4(const char **pp, int *out) {
  const char *p = *pp;
  int v = 0, d = 0;
  while (*p >= '0' && *p <= '9') {
    if (++d > 4) return false;
    v = v * 10 + (*p - '0');
    p++;
  }
  if (d == 0) return false;
  *out = v;
  *pp = p;
  return true;
}

bool comp_parse(const char *line, CompLine *o) {
  if (!o) return false;
  memset(o, 0, sizeof(*o));
  if (!line) {
    o->kind = COMP_BAD;
    return false;
  }
  if (strncmp(line, "HELLO ", 6) == 0) {
    const char *p = line + 6;
    for (int i = 0; i < COMP_TOKEN_LEN; i++) {
      if (!tok_char(p[i])) {
        o->kind = COMP_BAD;
        return false;
      }
      o->token[i] = p[i];
    }
    if (p[COMP_TOKEN_LEN] != ' ') {
      o->kind = COMP_BAD;
      return false;
    }
    p += COMP_TOKEN_LEN + 1;
    for (int i = 0; i < COMP_ID_LEN; i++) {
      if (!hex_char(p[i])) {
        o->kind = COMP_BAD;
        return false;
      }
      o->id[i] = p[i];
    }
    if (p[COMP_ID_LEN] != 0) {
      o->kind = COMP_BAD;
      return false;
    }
    o->kind = COMP_HELLO;
    return true;
  }
  if (strncmp(line, "STAT ", 5) == 0) {
    const char *p = line + 5;
    int cpu = 0, ram = 0;
    if (!parse_u100(&p, &cpu) || *p != ' ') {
      o->kind = COMP_BAD;
      return false;
    }
    p++;
    if (!parse_u100(&p, &ram)) {
      o->kind = COMP_BAD;
      return false;
    }
    if (*p == ' ') {
      p++;
      int n = 0;
      while (p[n]) {
        const unsigned char c = (unsigned char)p[n];
        if (n >= COMP_NOTE_MAX || c < 0x20 || c > 0x7E) {
          o->kind = COMP_BAD;
          return false;
        }
        o->note[n++] = (char)c;
      }
    } else if (*p != 0) {
      o->kind = COMP_BAD;
      return false;
    }
    o->cpu = (uint8_t)cpu;
    o->ram = (uint8_t)ram;
    o->kind = COMP_STAT;
    return true;
  }
  if (strncmp(line, "NOTIF ", 6) == 0) {
    const char *p = line + 6;
    int n = 0;
    if (*p == 0) {
      o->kind = COMP_BAD;
      return false;
    }
    while (p[n]) {
      const unsigned char c = (unsigned char)p[n];
      if (n >= COMP_NOTE_MAX || c < 0x20 || c > 0x7E) {
        o->kind = COMP_BAD;
        return false;
      }
      o->note[n++] = (char)c;
    }
    o->kind = COMP_NOTIF;
    return true;
  }
  if (strncmp(line, "NOW ", 4) == 0) {
    const char *p = line + 4;
    const char *bar = strchr(p, '|');
    if (!bar || strchr(bar + 1, '|') || !now_field(p, bar, o->title) || !now_field(bar + 1, bar + 1 + strlen(bar + 1), o->artist)) {
      memset(o, 0, sizeof(*o));
      o->kind = COMP_BAD;
      return false;
    }
    o->kind = COMP_NOW;
    return true;
  }
  if (strncmp(line, "ART ", 4) == 0) {
    const char *p = line + 4;
    int w = 0, h = 0, n = 0;
    if (!parse_dec4(&p, &w) || *p++ != ' ' || !parse_dec4(&p, &h) || *p++ != ' ' || !parse_dec4(&p, &n) || *p != 0) {
      o->kind = COMP_BAD;
      return false;
    }
    const bool clear = w == 0 && h == 0 && n == 0;
    const bool img = w >= 1 && w <= COMP_ART_MAX_DIM && h >= 1 && h <= COMP_ART_MAX_DIM && n == w * h * 2 && n <= COMP_ART_MAX;
    if (!clear && !img) {
      o->kind = COMP_BAD;
      return false;
    }
    o->art_w = (uint8_t)w;
    o->art_h = (uint8_t)h;
    o->art_n = (uint16_t)n;
    o->kind = COMP_ART;
    return true;
  }
  o->kind = COMP_BAD;
  return false;
}

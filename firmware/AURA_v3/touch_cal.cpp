// touch_cal.cpp - affine calibration math + NVS persistence (Arduino Preferences).
#include "touch_cal.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

struct CalBlob {
  uint16_t version;
  uint16_t size;
  int32_t k[6];
  uint32_t crc;  // CRC32 over all preceding bytes
};

uint32_t aura_crc32(const void *data, uint32_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static int32_t q16(double v) {
  double r = v * 65536.0;
  if (r > 2147483647.0) r = 2147483647.0;
  if (r < -2147483648.0) r = -2147483648.0;
  return (int32_t)lround(r);
}

void touch_cal_defaults(TouchCal *c) {
  // Measured landscape (rotation 1) edge mapping from config.h -> three
  // synthetic raw/screen pairs -> native (rotation 0) space -> same solver as
  // the calibration screen. The result is valid for any TFT_ROTATION because
  // the coefficients live in native panel space.
  //   screen x: raw Y from TOUCH_DEF_RY_AT_SX0 (x=0) to TOUCH_DEF_RY_AT_SX319 (x=319)
  //   screen y: raw X from TOUCH_DEF_RX_AT_SY0 (y=0) to TOUCH_DEF_RX_AT_SY239 (y=239)
  static const int32_t scr[3][2] = {{0, 0}, {319, 0}, {0, 239}};
  int32_t raw[3][2], nat[3][2];
  for (int i = 0; i < 3; i++) {
    const int32_t sx = scr[i][0], sy = scr[i][1];
    raw[i][0] = TOUCH_DEF_RX_AT_SY0 + ((TOUCH_DEF_RX_AT_SY239 - TOUCH_DEF_RX_AT_SY0) * sy) / 239;  // raw X
    raw[i][1] = TOUCH_DEF_RY_AT_SX0 + ((TOUCH_DEF_RY_AT_SX319 - TOUCH_DEF_RY_AT_SX0) * sx) / 319;  // raw Y
    touch_rot_from_screen(1, sx, sy, &nat[i][0], &nat[i][1]);
  }
  if (touch_cal_solve(raw, nat, c)) return;
  // Unreachable with sane constants: identity-ish fallback so the device stays usable.
  const double s = (double)(TOUCH_NATIVE_W - 1) / 3700.0;
  c->k[0] = q16(s); c->k[1] = 0; c->k[2] = q16(-s * 200);
  c->k[3] = 0; c->k[4] = q16(s * 320.0 / 240.0); c->k[5] = q16(-s * 200 * 320.0 / 240.0);
}

bool touch_cal_sane(const TouchCal *c) {
  // Area scale (native px^2 per raw count^2). Nominal ~0.0056 for a 240x320
  // panel spanning ~3700 raw counts per axis; accept a generous window.
  const double a = c->k[0] / 65536.0, b = c->k[1] / 65536.0;
  const double d = c->k[3] / 65536.0, e = c->k[4] / 65536.0;
  const double det = fabs(a * e - b * d);
  if (!(det > 0.0008 && det < 0.05)) return false;
  // The panel centre in raw space (~2048,2048) must land roughly on the panel.
  int32_t nx, ny;
  touch_cal_apply(c, 2048, 2048, &nx, &ny);
  return nx > -TOUCH_NATIVE_W && nx < 2 * TOUCH_NATIVE_W && ny > -TOUCH_NATIVE_H && ny < 2 * TOUCH_NATIVE_H;
}

bool touch_cal_load(TouchCal *c) {
  touch_cal_defaults(c);
  Preferences p;
  if (!p.begin("touch", true)) return false;
  CalBlob b;
  size_t n = p.getBytes("cal", &b, sizeof(b));
  p.end();
  if (n != sizeof(b) || b.version != TOUCH_CAL_VERSION || b.size != sizeof(b)) return false;
  if (aura_crc32(&b, offsetof(CalBlob, crc)) != b.crc) return false;
  TouchCal t;
  memcpy(t.k, b.k, sizeof(t.k));
  if (!touch_cal_sane(&t)) return false;
  *c = t;
  return true;
}

bool touch_cal_save(const TouchCal *c) {
  if (!touch_cal_sane(c)) return false;
  CalBlob b;
  memset(&b, 0, sizeof(b));
  b.version = TOUCH_CAL_VERSION;
  b.size = sizeof(b);
  memcpy(b.k, c->k, sizeof(b.k));
  b.crc = aura_crc32(&b, offsetof(CalBlob, crc));
  Preferences p;
  if (!p.begin("touch", false)) return false;
  size_t n = p.putBytes("cal", &b, sizeof(b));
  p.end();
  return n == sizeof(b);
}

bool touch_cal_erase(void) {
  Preferences p;
  if (!p.begin("touch", false)) return false;
  bool ok = p.remove("cal");
  p.end();
  return ok;
}

bool touch_cal_solve(const int32_t raw[3][2], const int32_t nat[3][2], TouchCal *out) {
  const double x0 = raw[0][0], y0 = raw[0][1];
  const double x1 = raw[1][0], y1 = raw[1][1];
  const double x2 = raw[2][0], y2 = raw[2][1];
  const double det = x0 * (y1 - y2) - y0 * (x1 - x2) + (x1 * y2 - x2 * y1);
  // Twice the raw triangle area; < 1e5 means the points are nearly collinear.
  if (fabs(det) < 1.0e5) return false;
  TouchCal c;
  for (int axis = 0; axis < 2; axis++) {
    const double n0 = nat[0][axis], n1 = nat[1][axis], n2 = nat[2][axis];
    const double ka = (n0 * (y1 - y2) - y0 * (n1 - n2) + (n1 * y2 - n2 * y1)) / det;
    const double kb = (x0 * (n1 - n2) - n0 * (x1 - x2) + (x1 * n2 - x2 * n1)) / det;
    const double kc = (x0 * (y1 * n2 - y2 * n1) - y0 * (x1 * n2 - x2 * n1) + n0 * (x1 * y2 - x2 * y1)) / det;
    c.k[axis * 3 + 0] = q16(ka);
    c.k[axis * 3 + 1] = q16(kb);
    c.k[axis * 3 + 2] = q16(kc);
  }
  if (!touch_cal_sane(&c)) return false;
  // Residual: the three source points must map back within 2 px (Q16 rounding only).
  for (int i = 0; i < 3; i++) {
    int32_t nx, ny;
    touch_cal_apply(&c, raw[i][0], raw[i][1], &nx, &ny);
    if (labs((long)(nx - nat[i][0])) > 2 || labs((long)(ny - nat[i][1])) > 2) return false;
  }
  *out = c;
  return true;
}

void touch_cal_apply(const TouchCal *c, int32_t rx, int32_t ry, int32_t *nx, int32_t *ny) {
  // 64-bit intermediate: k (Q16) * 12-bit raw can exceed int32.
  const int64_t x = (int64_t)c->k[0] * rx + (int64_t)c->k[1] * ry + (int64_t)c->k[2];
  const int64_t y = (int64_t)c->k[3] * rx + (int64_t)c->k[4] * ry + (int64_t)c->k[5];
  *nx = (int32_t)((x + 32768) >> 16);
  *ny = (int32_t)((y + 32768) >> 16);
}

void touch_rot_to_screen(int rot, int32_t nx, int32_t ny, int32_t *sx, int32_t *sy) {
  switch (rot & 3) {
    case 0:  *sx = nx;                       *sy = ny;                       break;
    case 1:  *sx = ny;                       *sy = (TOUCH_NATIVE_W - 1) - nx; break;
    case 2:  *sx = (TOUCH_NATIVE_W - 1) - nx; *sy = (TOUCH_NATIVE_H - 1) - ny; break;
    default: *sx = (TOUCH_NATIVE_H - 1) - ny; *sy = nx;                       break;
  }
}

void touch_rot_from_screen(int rot, int32_t sx, int32_t sy, int32_t *nx, int32_t *ny) {
  switch (rot & 3) {
    case 0:  *nx = sx;                       *ny = sy;                       break;
    case 1:  *nx = (TOUCH_NATIVE_W - 1) - sy; *ny = sx;                       break;
    case 2:  *nx = (TOUCH_NATIVE_W - 1) - sx; *ny = (TOUCH_NATIVE_H - 1) - sy; break;
    default: *nx = sy;                       *ny = (TOUCH_NATIVE_H - 1) - sx; break;
  }
}

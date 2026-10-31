// touch_cal.h - 3-point affine touch calibration (raw panel space, rotation 0)
// and screen rotation transform. Coefficients are Q16 fixed point.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_NATIVE_W 240   // panel native (rotation 0) width
#define TOUCH_NATIVE_H 320
#define TOUCH_CAL_VERSION 1

// native_x = (k[0]*rx + k[1]*ry + k[2]) >> 16
// native_y = (k[3]*rx + k[4]*ry + k[5]) >> 16
struct TouchCal {
  int32_t k[6];
};

void touch_cal_defaults(TouchCal *c);
// Load from NVS namespace "touch". Returns false (and fills defaults) when the
// blob is missing, has the wrong version/size, fails CRC32 or the sanity check.
bool touch_cal_load(TouchCal *c);
bool touch_cal_save(const TouchCal *c);
bool touch_cal_erase(void);

// Solve from three raw/native point pairs. Fails if the raw points are
// (nearly) collinear or the resulting scale is implausible.
bool touch_cal_solve(const int32_t raw[3][2], const int32_t nat[3][2], TouchCal *out);
bool touch_cal_sane(const TouchCal *c);
void touch_cal_apply(const TouchCal *c, int32_t rx, int32_t ry, int32_t *nx, int32_t *ny);

// Rotation (0..3) between native panel space and screen space.
void touch_rot_to_screen(int rot, int32_t nx, int32_t ny, int32_t *sx, int32_t *sy);
void touch_rot_from_screen(int rot, int32_t sx, int32_t sy, int32_t *nx, int32_t *ny);

uint32_t aura_crc32(const void *data, uint32_t len);

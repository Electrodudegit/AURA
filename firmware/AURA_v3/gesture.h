// gesture.h - TAP / LONG / SWIPE recogniser fed by debounced touch samples.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "touch_xpt2046.h"

enum GestType : uint8_t {
  GEST_NONE = 0, GEST_TAP, GEST_LONG, GEST_SWIPE_L, GEST_SWIPE_R, GEST_SWIPE_U, GEST_SWIPE_D, GEST_CANCEL
};

struct GestEvent {
  uint8_t type;
  int16_t x, y;      // event position (screen)
  int16_t x0, y0;    // press position
  uint16_t rx, ry, z;
  uint32_t dt_ms;
  uint32_t t_ms;     // millis() when the event was generated (for [lat])
};

bool gesture_init(void);
QueueHandle_t gesture_queue(void);
// Feed every touch tick. Emits events into the queue and logs a [touch] line.
void gesture_feed(const TouchSample *s, uint32_t now_ms);
void gesture_reset(void);
const char *gesture_name(uint8_t type);
uint32_t gesture_dropped(void);

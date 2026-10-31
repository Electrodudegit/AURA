#include "gesture.h"
#include "config.h"
#include "aura_log.h"

#define GEST_Q_LEN 8
static StaticQueue_t s_qbuf;
static uint8_t s_qstore[GEST_Q_LEN * sizeof(GestEvent)];
static QueueHandle_t s_q = nullptr;
static uint32_t s_dropped = 0;

static bool s_pressed = false;
static bool s_long_fired = false;
static uint32_t s_t0 = 0;
static int16_t s_x0 = 0, s_y0 = 0;
static int32_t s_maxd2 = 0;

bool gesture_init(void) {
  s_q = xQueueCreateStatic(GEST_Q_LEN, sizeof(GestEvent), s_qstore, &s_qbuf);
  return s_q != nullptr;
}
QueueHandle_t gesture_queue(void) { return s_q; }
uint32_t gesture_dropped(void) { return s_dropped; }
void gesture_reset(void) { s_pressed = false; s_long_fired = false; }

const char *gesture_name(uint8_t t) {
  switch (t) {
    case GEST_TAP: return "TAP";
    case GEST_LONG: return "LONG";
    case GEST_SWIPE_L: return "SWIPE_L";
    case GEST_SWIPE_R: return "SWIPE_R";
    case GEST_SWIPE_U: return "SWIPE_U";
    case GEST_SWIPE_D: return "SWIPE_D";
    case GEST_CANCEL: return "CANCEL";
    default: return "NONE";
  }
}

static void emit(uint8_t type, const TouchSample *s, uint32_t dt, uint32_t now) {
  GestEvent ev;
  ev.type = type;
  ev.x = s->x; ev.y = s->y;
  ev.x0 = s_x0; ev.y0 = s_y0;
  ev.rx = s->rx; ev.ry = s->ry; ev.z = s->z;
  ev.dt_ms = dt;
  ev.t_ms = now;
  if (xQueueSend(s_q, &ev, 0) != pdTRUE) s_dropped++;
  aura_log("[touch] ev=%s x=%d y=%d raw=%u,%u z=%u dt=%lums", gesture_name(type), ev.x, ev.y,
           (unsigned)ev.rx, (unsigned)ev.ry, (unsigned)ev.z, (unsigned long)dt);
}

void gesture_feed(const TouchSample *s, uint32_t now) {
  const int32_t tap2 = (int32_t)GEST_TAP_MAX_PX * GEST_TAP_MAX_PX;
  if (s->down && !s_pressed) {
    s_pressed = true;
    s_long_fired = false;
    s_t0 = now;
    s_x0 = s->x; s_y0 = s->y;
    s_maxd2 = 0;
    return;
  }
  if (s->down && s_pressed) {
    const int32_t dx = s->x - s_x0, dy = s->y - s_y0;
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 > s_maxd2) s_maxd2 = d2;
    if (!s_long_fired && s_maxd2 < tap2 && (uint32_t)(now - s_t0) >= GEST_LONG_MS) {
      s_long_fired = true;
      emit(GEST_LONG, s, now - s_t0, now);
    }
    return;
  }
  if (!s->down && s_pressed) {
    s_pressed = false;
    // Release time = last valid sample (debounce adds TOUCH_DEBOUNCE_UP ticks).
    const uint32_t dt = (uint32_t)(s->t_ms - s_t0);
    if (s_long_fired) return;  // LONG swallows its release
    const int32_t dx = s->x - s_x0, dy = s->y - s_y0;
    const int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx > GEST_SWIPE_MIN_PX && adx * 10 >= ady * GEST_SWIPE_DOM_X10) {
      emit(dx < 0 ? GEST_SWIPE_L : GEST_SWIPE_R, s, dt, now);
    } else if (ady > GEST_SWIPE_MIN_PX && ady * 10 >= adx * GEST_SWIPE_DOM_X10) {
      emit(dy < 0 ? GEST_SWIPE_U : GEST_SWIPE_D, s, dt, now);
    } else if (dt < GEST_TAP_MAX_MS && s_maxd2 < tap2) {
      emit(GEST_TAP, s, dt, now);
    } else {
      emit(GEST_CANCEL, s, dt, now);
    }
  }
}

#include "prof.h"
#ifdef AURA_HOST
#include <chrono>
uint32_t prof_now_us(void) {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#else
#include <esp_timer.h>
uint32_t prof_now_us(void) { return (uint32_t)esp_timer_get_time(); }
#endif
const char *const k_prof_names[PL_COUNT] = {"other", "bg", "aurora", "tiles", "text", "part", "face"};
static uint32_t s_acc[PL_COUNT];
static uint8_t s_stack[8];
static int s_sp = 0;
static uint32_t s_t = 0;
void prof_frame_reset(void) {
  for (int i = 0; i < PL_COUNT; i++) s_acc[i] = 0;
  s_sp = 0;
  s_stack[0] = PL_OTHER;
  s_t = prof_now_us();
}
static inline void charge(uint32_t now) {
  s_acc[s_stack[s_sp]] += now - s_t;
  s_t = now;
}
void prof_push(uint8_t l) {
  const uint32_t now = prof_now_us();
  charge(now);
  if (s_sp < 7) s_stack[++s_sp] = l;
}
void prof_pop(void) {
  const uint32_t now = prof_now_us();
  charge(now);
  if (s_sp > 0) s_sp--;
}
void prof_get(uint32_t out[PL_COUNT]) {
  charge(prof_now_us());
  for (int i = 0; i < PL_COUNT; i++) out[i] = s_acc[i];
}

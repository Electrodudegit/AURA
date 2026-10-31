// keypad.h - on-screen QWERTY for short text (Oracle questions; later Wi-Fi / API fields).
// Chrome is the AURA tile language: C_BG field, C_TILE keys, Q03 focus. No filled accent button.
#pragma once
#include "ui_core.h"

enum KpAct : uint8_t { KP_NONE = 0, KP_EDIT, KP_SUBMIT, KP_BACK };
enum KpMode : uint8_t { KP_LOWER = 0, KP_SHIFT, KP_CAPS, KP_NUM };

#define KP_NKEYS 31

struct Keypad {
  char *buf;
  uint16_t cap;        // bytes including the NUL
  uint16_t len;
  uint16_t max_chars;  // UI limit (Oracle: 200)
  uint8_t mode;        // KpMode
  uint8_t focus;       // 0 .. KP_NKEYS-1
  uint32_t flash_until;
  uint8_t flash_i;
};

// buf is not cleared. len is taken from the existing C string (capped).
void keypad_bind(Keypad *k, char *buf, uint16_t cap, uint16_t max_chars);
// HOLD, a left-edge swipe, or a top-left tap returns KP_BACK (caller consumes it).
// ASK returns KP_SUBMIT even when the buffer is empty.
KpAct keypad_input(Keypad *k, const UiIn &in, uint32_t now);
// Field + keys only. The caller fills the screen and draws w_status (badge n/max).
void keypad_draw(const Keypad *k, uint32_t now);

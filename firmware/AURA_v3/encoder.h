// encoder.h - KY-040 quadrature encoder (ISR) + push switch (task-context debounce).
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum EncEvent : uint8_t { ENC_EV_NONE = 0, ENC_EV_CLICK, ENC_EV_LONG };

bool encoder_init(void);
// Whole detents since the last call (+cw / -ccw, direction is wiring dependent).
int32_t encoder_take_detents(void);
// Same, and *t_first_ms = time of the edge that completed the first unconsumed
// detent (valid only when the return value is non-zero).
int32_t encoder_take_detents_ts(uint32_t *t_first_ms);
// Raw quadrature transitions since boot (diagnostics).
int32_t encoder_raw(void);
uint32_t encoder_invalid_transitions(void);
// Call every ~10 ms from one task. Returns at most one switch event.
EncEvent encoder_poll_switch(uint32_t now_ms);
bool encoder_switch_down(void);

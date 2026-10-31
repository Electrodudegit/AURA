// aura_log.h - heap-free serial logging (vsnprintf into a stack buffer).
// Task context only. Never call from an ISR. Never log secrets.
#pragma once
#define AURA_LOG_MAX 256  // max chars per line incl. CRLF; longer lines end in "~"
void aura_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

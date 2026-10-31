#include "aura_log.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

// Print::printf() mallocs for lines > 64 bytes; this does not.
void aura_log(const char *fmt, ...) {
  char buf[AURA_LOG_MAX];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t len = (size_t)n;
  if (len > sizeof(buf) - 3) {  // truncated: vsnprintf wrote sizeof(buf)-3 chars + NUL
    len = sizeof(buf) - 3;
    buf[len - 1] = '~';
  }
  buf[len++] = '\r';
  buf[len++] = '\n';
  Serial.write((const uint8_t *)buf, len);  // HardwareSerial write is mutex-protected
}

#include "diag.h"

#include <stdarg.h>

namespace diag {

namespace {
constexpr int LINES = 80;
constexpr int WIDTH = 140;
char s_ring[LINES][WIDTH];
uint32_t s_next = 0; // total lines written; slot = s_next % LINES

void push(const char *msg) {
  char *slot = s_ring[s_next % LINES];
  snprintf(slot, WIDTH, "%8lu %s", (unsigned long)millis(), msg);
  s_next++;
}
} // namespace

void log(const char *fmt, ...) {
  char buf[WIDTH];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  push(buf);
}

void evt(const char *fmt, ...) {
  char buf[WIDTH];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  push(buf);
  Serial.print("EVT ");
  Serial.println(buf);
}

void dump() {
  uint32_t first = s_next > LINES ? s_next - LINES : 0;
  Serial.printf("log: %lu lines total, showing %lu (ms since boot, newest last)\n", (unsigned long)s_next,
                (unsigned long)(s_next - first));
  for (uint32_t i = first; i < s_next; i++) Serial.println(s_ring[i % LINES]);
}

} // namespace diag

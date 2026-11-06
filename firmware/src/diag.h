// In-RAM event log, so behavior can be inspected after the fact even when nothing was reading the
// serial port at the time (`log` console command dumps it).
#pragma once
#include <Arduino.h>

namespace diag {
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2))); // ring buffer only
void evt(const char *fmt, ...) __attribute__((format(printf, 1, 2))); // "EVT ..." to Serial + ring
void dump();                                                          // print the ring, oldest first
} // namespace diag

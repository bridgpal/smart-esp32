// Line-based serial console. Every command ends with a line "OK" or "ERR <reason>",
// so scripts/devcmd.py can drive the board non-interactively. Async events print as "EVT ...".
#pragma once
#include <Arduino.h>

namespace console {
void poll(); // call from loop(); reads Serial and runs complete lines
void run(String line);
} // namespace console

// Board bring-up: I2C bus, TCA9555 expander (amp enable + keys), RGB ring.
#pragma once
#include <Arduino.h>

namespace hw {

bool begin();                  // I2C + expander + LEDs. Returns false if the expander is missing.
bool expanderOk();

// I2C
int scanI2C(uint8_t *found, int max); // returns count

// Expander
bool expanderWrite(uint8_t pin, bool level);
bool expanderReadAll(uint16_t &levels);

// Speaker amp
bool setAmp(bool on);

// Keys (EXIO9..11, plus BOOT on GPIO0). Bit set = pressed.
enum Key : uint8_t { KEY_9 = 0, KEY_10 = 1, KEY_11 = 2, KEY_BOOT = 3, KEY_COUNT = 4 };
const char *keyName(uint8_t key);
uint8_t readKeys();
uint32_t keyReadFailures(); // expander I2C reads that failed since boot
// Debounced edge detection; call often. Returns bitmask of keys newly pressed since last call.
uint8_t pollKeyPresses();
// Re-read the keys and treat their current state as already seen (call after a long blocking
// action so a key still held, or released meanwhile, isn't misread as a new press).
void resyncKeys();

// RGB ring
void setAll(uint8_t r, uint8_t g, uint8_t b);
void setPixel(int i, uint8_t r, uint8_t g, uint8_t b);
void show();
void spin(uint8_t r, uint8_t g, uint8_t b, int laps = 1, int stepMs = 40);

} // namespace hw

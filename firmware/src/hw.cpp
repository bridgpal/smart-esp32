#include "hw.h"

#include <Adafruit_NeoPixel.h>
#include <Wire.h>

#include "board.h"

namespace hw {

namespace {
// TCA9555 registers
constexpr uint8_t REG_INPUT0 = 0x00;
constexpr uint8_t REG_OUTPUT0 = 0x02;
constexpr uint8_t REG_CONFIG0 = 0x06;

// Direction: 1 = input. Outputs match Waveshare's factory firmware: EXIO0,1,5,6,8.
constexpr uint16_t EXIO_OUTPUT_MASK = (1u << EXIO_LCD_RST) | (1u << EXIO_TP_RST) |
                                      (1u << EXIO_CAM_PWDN) | (1u << EXIO_CAM_SEL) |
                                      (1u << EXIO_PA_EN);
// Initial levels: LCD/touch out of reset, camera powered down, amp off.
constexpr uint16_t EXIO_INITIAL = (1u << EXIO_LCD_RST) | (1u << EXIO_TP_RST) | (1u << EXIO_CAM_PWDN);

Adafruit_NeoPixel ring(RGB_COUNT, PIN_RGB, NEO_RGB + NEO_KHZ800);
bool s_expanderOk = false;
uint16_t s_outputs = EXIO_INITIAL;
uint8_t s_stable = 0, s_lastRaw = 0;
uint32_t s_lastChange = 0;

bool writeReg16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(I2C_ADDR_TCA9555);
  Wire.write(reg);
  Wire.write(value & 0xFF);
  Wire.write(value >> 8);
  return Wire.endTransmission() == 0;
}

bool readReg16(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(I2C_ADDR_TCA9555);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)I2C_ADDR_TCA9555, (uint8_t)2) != 2) return false;
  value = Wire.read();
  value |= (uint16_t)Wire.read() << 8;
  return true;
}
} // namespace

bool begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  // Outputs first so pins don't glitch when switched from input.
  s_expanderOk = writeReg16(REG_OUTPUT0, s_outputs) &&
                 writeReg16(REG_CONFIG0, (uint16_t)~EXIO_OUTPUT_MASK);

  ring.begin();
  ring.setBrightness(80);
  ring.clear();
  ring.show();
  return s_expanderOk;
}

bool expanderOk() { return s_expanderOk; }

int scanI2C(uint8_t *found, int max) {
  int n = 0;
  for (uint8_t addr = 0x08; addr < 0x78 && n < max; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) found[n++] = addr;
  }
  return n;
}

bool expanderWrite(uint8_t pin, bool level) {
  uint16_t next = level ? (s_outputs | (1u << pin)) : (s_outputs & ~(1u << pin));
  if (!writeReg16(REG_OUTPUT0, next)) return false;
  s_outputs = next;
  return true;
}

bool expanderReadAll(uint16_t &levels) { return readReg16(REG_INPUT0, levels); }

bool setAmp(bool on) { return expanderWrite(EXIO_PA_EN, on); }

const char *keyName(uint8_t key) {
  switch (key) {
  case KEY_9: return "K1";
  case KEY_10: return "K2";
  case KEY_11: return "K3";
  case KEY_BOOT: return "BOOT";
  default: return "?";
  }
}

uint8_t readKeys() {
  uint8_t pressed = 0;
  uint16_t levels;
  if (s_expanderOk && expanderReadAll(levels)) {
    if (!(levels & (1u << EXIO_KEY_9))) pressed |= 1 << KEY_9;
    if (!(levels & (1u << EXIO_KEY_10))) pressed |= 1 << KEY_10;
    if (!(levels & (1u << EXIO_KEY_11))) pressed |= 1 << KEY_11;
  }
  if (digitalRead(PIN_BOOT) == LOW) pressed |= 1 << KEY_BOOT;
  return pressed;
}

uint8_t pollKeyPresses() {
  uint8_t raw = readKeys();
  uint32_t now = millis();
  if (raw != s_lastRaw) {
    s_lastRaw = raw;
    s_lastChange = now;
    return 0;
  }
  if (raw == s_stable || now - s_lastChange < 30) return 0;
  uint8_t newlyPressed = raw & ~s_stable;
  s_stable = raw;
  return newlyPressed;
}

void resyncKeys() {
  s_lastRaw = s_stable = readKeys();
  s_lastChange = millis();
}

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  ring.fill(ring.Color(r, g, b));
  ring.show();
}

void setPixel(int i, uint8_t r, uint8_t g, uint8_t b) { ring.setPixelColor(i, ring.Color(r, g, b)); }

void show() { ring.show(); }

void spin(uint8_t r, uint8_t g, uint8_t b, int laps, int stepMs) {
  for (int step = 0; step < laps * RGB_COUNT; step++) {
    ring.clear();
    ring.setPixelColor(step % RGB_COUNT, ring.Color(r, g, b));
    ring.show();
    delay(stepMs);
  }
  ring.clear();
  ring.show();
}

} // namespace hw

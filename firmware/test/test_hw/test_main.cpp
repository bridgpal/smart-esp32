// On-device hardware tests. Run with: make test
#include <Arduino.h>
#include <unity.h>

#include "board.h"
#include "hw.h"

static bool hasAddr(uint8_t addr) {
  uint8_t found[32];
  int n = hw::scanI2C(found, 32);
  for (int i = 0; i < n; i++)
    if (found[i] == addr) return true;
  return false;
}

void test_psram_present() { TEST_ASSERT_GREATER_OR_EQUAL_UINT32(7 * 1024 * 1024, ESP.getPsramSize()); }
void test_flash_16mb() { TEST_ASSERT_EQUAL_UINT32(16 * 1024 * 1024, ESP.getFlashChipSize()); }
void test_expander_present() { TEST_ASSERT_TRUE(hw::expanderOk()); }
void test_codec_present() { TEST_ASSERT_TRUE_MESSAGE(hasAddr(I2C_ADDR_ES8311), "ES8311 not on I2C"); }
void test_mic_adc_present() { TEST_ASSERT_TRUE_MESSAGE(hasAddr(I2C_ADDR_ES7210), "ES7210 not on I2C"); }

void test_amp_toggle() {
  TEST_ASSERT_TRUE(hw::setAmp(true));
  TEST_ASSERT_TRUE(hw::setAmp(false));
}

void test_no_keys_held() { TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, hw::readKeys(), "a key reads as pressed at rest"); }

void test_psram_alloc() {
  void *p = ps_malloc(2 * 1024 * 1024);
  TEST_ASSERT_NOT_NULL(p);
  memset(p, 0xA5, 2 * 1024 * 1024);
  TEST_ASSERT_EQUAL_UINT8(0xA5, ((uint8_t *)p)[2 * 1024 * 1024 - 1]);
  free(p);
}

void setup() {
  delay(2000); // let the USB serial attach
  hw::begin();
  UNITY_BEGIN();
  RUN_TEST(test_psram_present);
  RUN_TEST(test_flash_16mb);
  RUN_TEST(test_psram_alloc);
  RUN_TEST(test_expander_present);
  RUN_TEST(test_codec_present);
  RUN_TEST(test_mic_adc_present);
  RUN_TEST(test_amp_toggle);
  RUN_TEST(test_no_keys_held);
  UNITY_END();
}

void loop() {}

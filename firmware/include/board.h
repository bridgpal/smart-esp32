// Hardware map for the SMKTelec "ESP32-S3 AI Smart Speaker" board.
// Electrically this matches the Waveshare ESP32-S3-AUDIO-Board; pins below come from
// Waveshare's factory_01 demo (bsp_board.h, tca9555_driver.c, button_driver.c) and the
// wiki pin tables. Verified on this unit: run `make cmd C=scan` and compare I2C addresses.
#pragma once
#include <stdint.h>

// --- I2C (shared by codec, mic ADC, IO expander, RTC, touch) ---
#define PIN_I2C_SDA 11
#define PIN_I2C_SCL 10
#define I2C_FREQ_HZ 400000

#define I2C_ADDR_ES8311 0x18   // speaker DAC / codec
#define I2C_ADDR_TCA9555 0x20  // 16-bit IO expander (A2..A0 = 000)
#define I2C_ADDR_ES7210 0x40   // 4-ch mic ADC (dual mic array + AEC reference)
#define I2C_ADDR_PCF85063 0x51 // RTC

// --- I2S (ES8311 out + ES7210 in share clocks) ---
#define PIN_I2S_MCLK 12
#define PIN_I2S_BCLK 13
#define PIN_I2S_WS 14
#define PIN_I2S_DOUT 16 // ESP32 -> ES8311 (speaker)
#define PIN_I2S_DIN 15  // ES7210 -> ESP32 (mics)

// --- RGB ring (WS2812-type, RGB byte order, not the usual GRB; verified on this unit) ---
#define PIN_RGB 38
#define RGB_COUNT 7

// --- TF card (SDMMC 1-bit) ---
#define PIN_SD_CLK 40
#define PIN_SD_CMD 42
#define PIN_SD_D0 41

// --- Native GPIO buttons ---
#define PIN_BOOT 0

// --- TCA9555 expander pins (EXIO numbering, 0..15) ---
#define EXIO_LCD_RST 0
#define EXIO_TP_RST 1
#define EXIO_TP_INT 2
#define EXIO_SD_CS 3
#define EXIO_CAM_PWDN 5
#define EXIO_CAM_SEL 6 // high: CAM_PCLK=GPIO44/XCLK=GPIO43, low: GPIO19/GPIO20
#define EXIO_PA_EN 8   // speaker power amp enable, active high
#define EXIO_KEY_9 9   // K1 (volume up in factory firmware), active low
#define EXIO_KEY_10 10 // K2 (play/pause), active low
#define EXIO_KEY_11 11 // K3 (volume down), active low

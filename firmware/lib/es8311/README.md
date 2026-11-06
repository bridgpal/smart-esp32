ES8311 codec driver from Espressif (Apache-2.0, SPDX header in each file), as adapted to Arduino
`Wire` in Waveshare's ESP32-S3-AUDIO-Board demo. Local change: es8311.h includes
`driver/i2c_types.h` instead of the legacy `driver/i2c.h`, which conflicts with Wire's I2C driver.

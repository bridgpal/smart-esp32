ES7210 mic ADC driver from Espressif (Apache-2.0, SPDX header in each file), via Waveshare's
ESP32-S3-AUDIO-Board demo. Local changes: compiled as C++, register writes go through Arduino
`Wire` instead of the legacy ESP-IDF `i2c_cmd` API (which conflicts with Wire's driver), and the
header includes `driver/i2c_types.h` instead of `driver/i2c.h`.

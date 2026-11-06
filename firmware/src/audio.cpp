#include "audio.h"

#include <ESP_I2S.h>
#include <es7210.h>
#include <es8311.h>
#include <math.h>

#include "board.h"
#include "hw.h"

namespace audio {

namespace {
I2SClass s_i2s;
es8311_handle_t s_dac = nullptr;
es7210_dev_handle_t s_adc = nullptr;
bool s_ok = false;
int s_volume = 70;

constexpr size_t FRAMES = 512; // per I2S transfer: 32 ms at 16 kHz
int16_t s_stereo[FRAMES * 2];

void ampOn() {
  hw::setAmp(true);
  delay(30); // let the amp settle so the first syllable isn't clipped
}

void ampOff() {
  memset(s_stereo, 0, sizeof(s_stereo)); // drain DMA with silence before cutting the amp
  s_i2s.write((const uint8_t *)s_stereo, sizeof(s_stereo));
  s_i2s.write((const uint8_t *)s_stereo, sizeof(s_stereo));
  hw::setAmp(false);
}

// Mono -> stereo (ES8311 plays the left slot; duplicate so either works).
void writeMono(const int16_t *mono, size_t n) {
  while (n) {
    size_t k = min(n, FRAMES);
    for (size_t i = 0; i < k; i++) s_stereo[2 * i] = s_stereo[2 * i + 1] = mono[i];
    s_i2s.write((const uint8_t *)s_stereo, k * 4);
    mono += k;
    n -= k;
  }
}
} // namespace

bool begin() {
  // I2S first: both codecs are clocked from MCLK (256 x 16 kHz = 4.096 MHz).
  s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  s_i2s.setTimeout(1000);
  if (!s_i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    log_e("i2s begin failed");
    return false;
  }

  s_dac = es8311_create(I2C_NUM_0, I2C_ADDR_ES8311);
  const es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = (int)(SAMPLE_RATE * 256),
      .sample_frequency = (int)SAMPLE_RATE,
  };
  if (!s_dac || es8311_init(s_dac, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    log_e("es8311 init failed");
    return false;
  }
  es8311_microphone_config(s_dac, false);

  // Mic settings follow Waveshare's demo (MIC_MSM.h), but standard I2S instead of TDM so the
  // ESP32's stereo RX lines up with MIC1/MIC2.
  es7210_i2c_config_t i2c = {.i2c_port = I2C_NUM_0, .i2c_addr = I2C_ADDR_ES7210};
  es7210_codec_config_t cfg = {};
  cfg.sample_rate_hz = SAMPLE_RATE;
  cfg.mclk_ratio = 256;
  cfg.i2s_format = ES7210_I2S_FMT_I2S;
  cfg.bit_width = ES7210_I2S_BITS_16B;
  cfg.mic_bias = ES7210_MIC_BIAS_2V87;
  cfg.mic_gain = ES7210_MIC_GAIN_30DB;
  cfg.flags.tdm_enable = false;
  if (es7210_new_codec(&i2c, &s_adc) != ESP_OK || es7210_config_codec(s_adc, &cfg) != ESP_OK ||
      es7210_config_volume(s_adc, 0) != ESP_OK) {
    log_e("es7210 init failed");
    return false;
  }

  s_ok = setVolume(s_volume);
  return s_ok;
}

bool ok() { return s_ok; }

bool setVolume(int percent) {
  if (!s_dac) return false;
  s_volume = constrain(percent, 0, 100);
  return es8311_voice_volume_set(s_dac, s_volume, nullptr) == ESP_OK;
}

int volume() { return s_volume; }

void tone(int hz, int ms) {
  if (!s_ok) return;
  int16_t mono[FRAMES];
  int total = (int)((int64_t)SAMPLE_RATE * ms / 1000);
  int fade = SAMPLE_RATE / 200; // 5 ms ramps avoid clicks
  ampOn();
  for (int i = 0; i < total;) {
    int n = min((int)FRAMES, total - i);
    for (int j = 0; j < n; j++, i++) {
      float env = min(1.0f, min(i, total - i) / (float)fade);
      mono[j] = (int16_t)(sinf(2.0f * PI * hz * i / SAMPLE_RATE) * 12000.0f * env);
    }
    writeMono(mono, n);
  }
  ampOff();
}

void playPcm16(const uint8_t *data, size_t bytes) {
  if (!s_ok || !data || bytes < 2) return;
  ampOn();
  writeMono((const int16_t *)data, bytes / 2); // PSRAM buffers from ps_malloc are aligned
  ampOff();
}

size_t record(uint8_t *buf, size_t maxBytes, const std::function<bool()> &keepGoing, float *rmsLeft,
              float *rmsRight) {
  if (!s_ok) return 0;
  // Throw away what the RX DMA buffered while nobody was reading (~100 ms).
  for (int i = 0; i < 4; i++) s_i2s.readBytes((char *)s_stereo, sizeof(s_stereo));

  int16_t *out = (int16_t *)buf;
  size_t maxSamples = maxBytes / 2, n = 0;
  double sumL = 0, sumR = 0;
  while (n < maxSamples && keepGoing()) {
    size_t got = s_i2s.readBytes((char *)s_stereo, sizeof(s_stereo)) / 4;
    for (size_t i = 0; i < got && n < maxSamples; i++, n++) {
      int16_t l = s_stereo[2 * i], r = s_stereo[2 * i + 1];
      sumL += (double)l * l;
      sumR += (double)r * r;
      out[n] = l; // MIC1
    }
  }
  if (rmsLeft) *rmsLeft = n ? sqrt(sumL / n) : 0;
  if (rmsRight) *rmsRight = n ? sqrt(sumR / n) : 0;
  return n * 2;
}

} // namespace audio

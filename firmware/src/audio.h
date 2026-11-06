// Board audio: one full-duplex I2S bus, 16 kHz, 16-bit stereo frames, MCLK = 256 x fs.
//   out: ES8311 codec -> power amp (TCA9555 EXIO8) -> speaker
//   in:  ES7210 mic ADC, standard I2S: left = MIC1, right = MIC2
// Public PCM is always 16 kHz, 16-bit signed little-endian, mono.
#pragma once
#include <Arduino.h>
#include <functional>

namespace audio {

constexpr uint32_t SAMPLE_RATE = 16000;

bool begin(); // I2S + both codecs. Returns false if either codec doesn't answer.
bool ok();

bool setVolume(int percent); // 0..100, speaker DAC volume
int volume();

void tone(int hz, int ms);                         // sine beep
void playPcm16(const uint8_t *data, size_t bytes); // blocking, mono

// Streaming playback: begin, write mono PCM in any-sized pieces (odd bytes carried over), end.
void beginPlayback();
void writePcm(const uint8_t *data, size_t bytes);
void endPlayback();

// Record mono PCM into buf until keepGoing() returns false or the buffer is full.
// Returns bytes written. Also reports per-channel RMS (0..32767) for mic diagnostics.
size_t record(uint8_t *buf, size_t maxBytes, const std::function<bool()> &keepGoing, float *rmsLeft = nullptr,
              float *rmsRight = nullptr);

} // namespace audio

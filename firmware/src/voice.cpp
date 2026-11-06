#include "voice.h"

#include "audio.h"
#include "hw.h"
#include "net.h"

namespace voice {

bool talk(const std::function<bool()> &keepGoing) {
  if (!audio::ok()) {
    Serial.println("EVT talk: audio not initialised");
    return false;
  }
  const size_t maxBytes = MAX_RECORD_SECONDS * audio::SAMPLE_RATE * 2;
  uint8_t *rec = (uint8_t *)ps_malloc(maxBytes);
  if (!rec) {
    Serial.println("EVT talk: out of PSRAM");
    return false;
  }

  hw::setAll(60, 0, 0); // red: listening
  Serial.println("EVT talk: listening");
  float rmsL = 0, rmsR = 0;
  uint32_t t0 = millis();
  size_t len = audio::record(rec, maxBytes, keepGoing, &rmsL, &rmsR);
  Serial.printf("EVT talk: recorded %.1f s (mic rms L=%.0f R=%.0f)\n", len / 2.0f / audio::SAMPLE_RATE, rmsL, rmsR);

  if (len < audio::SAMPLE_RATE * 2 / 2) { // under half a second: treat as an accidental tap
    free(rec);
    hw::setAll(0, 0, 0);
    Serial.println("EVT talk: too short, ignored");
    return false;
  }

  hw::setAll(0, 0, 60); // blue: thinking
  uint32_t t1 = millis();
  net::Binary b = net::apiBinary("POST", "/api/device/talk", rec, len, "application/octet-stream", 4 * 1024 * 1024);
  free(rec);
  uint32_t t2 = millis();

  if (b.transcript.length()) Serial.printf("EVT talk: heard \"%s\"\n", b.transcript.c_str());
  if (b.reply.length()) Serial.printf("EVT talk: reply \"%s\"\n", b.reply.c_str());
  if (!b.data) {
    Serial.printf("EVT talk: failed status=%d %s %s\n", b.res.status, b.res.error.c_str(), b.res.body.substring(0, 300).c_str());
    hw::setAll(60, 0, 0);
    delay(400);
    hw::setAll(0, 0, 0);
    return false;
  }
  Serial.printf("EVT talk: %u bytes of reply audio, round trip %lu ms (recording %lu ms)\n", (unsigned)b.len,
                (unsigned long)(t2 - t1), (unsigned long)(t1 - t0));

  hw::setAll(0, 60, 0); // green: speaking
  audio::playPcm16(b.data, b.len);
  free(b.data);
  hw::setAll(0, 0, 0);
  return true;
}

} // namespace voice

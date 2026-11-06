#include "voice.h"

#include "audio.h"
#include "hw.h"
#include "net.h"

namespace voice {

namespace {
// Parses the /api/device/talk-stream frames ([type][u32 LE length][payload]) as they arrive and
// plays audio frames immediately, after a short prebuffer to ride out network jitter.
class FramePlayer : public Stream {
public:
  explicit FramePlayer(uint32_t t0) : t0_(t0) {}

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *d, size_t n) override {
    size_t left = n;
    while (left) {
      if (hdrLen_ < 5) {
        hdr_[hdrLen_++] = *d++;
        left--;
        if (hdrLen_ == 5) {
          type_ = (char)hdr_[0];
          remaining_ = hdr_[1] | (hdr_[2] << 8) | (hdr_[3] << 16) | ((uint32_t)hdr_[4] << 24);
          text_ = "";
          if (!remaining_) endFrame();
        }
        continue;
      }
      size_t take = min((size_t)remaining_, left);
      if (type_ == 'A') onAudio(d, take);
      else if (text_.length() < 600) text_.concat((const char *)d, min(take, (size_t)(600 - text_.length())));
      d += take;
      left -= take;
      remaining_ -= take;
      if (!remaining_) endFrame();
    }
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  void finish() {
    if (!playing_ && prebufLen_) start();
    audio::endPlayback();
  }
  bool gotEnd() const { return ended_; }
  bool hadError() const { return error_; }
  size_t audioBytes() const { return audioBytes_; }
  uint32_t firstAudioMs() const { return firstAudioMs_; }

private:
  static constexpr size_t PREBUF = audio::SAMPLE_RATE * 2 / 4; // 250 ms

  void endFrame() {
    hdrLen_ = 0;
    switch (type_) {
    case 'T': Serial.printf("EVT talk: heard \"%s\" (%lu ms)\n", text_.c_str(), (unsigned long)(millis() - t0_)); break;
    case 'R': Serial.printf("EVT talk: reply \"%s\" (%lu ms)\n", text_.c_str(), (unsigned long)(millis() - t0_)); break;
    case 'E': Serial.printf("EVT talk: server error %s\n", text_.c_str()); error_ = true; break;
    case 'Z': ended_ = true; break;
    default: break;
    }
  }

  void onAudio(const uint8_t *d, size_t n) {
    if (!firstAudioMs_) firstAudioMs_ = millis() - t0_;
    audioBytes_ += n;
    if (playing_) return audio::writePcm(d, n);
    size_t take = min(n, PREBUF - prebufLen_);
    memcpy(prebuf_ + prebufLen_, d, take);
    prebufLen_ += take;
    if (prebufLen_ == PREBUF) {
      start();
      audio::writePcm(d + take, n - take);
    }
  }

  void start() {
    playing_ = true;
    hw::setAll(0, 60, 0); // green: speaking
    Serial.printf("EVT talk: speaking (%lu ms)\n", (unsigned long)(millis() - t0_));
    audio::beginPlayback();
    audio::writePcm(prebuf_, prebufLen_);
  }

  uint32_t t0_;
  uint8_t hdr_[5];
  size_t hdrLen_ = 0;
  char type_ = 0;
  uint32_t remaining_ = 0;
  String text_;
  bool playing_ = false, ended_ = false, error_ = false;
  uint8_t prebuf_[PREBUF];
  size_t prebufLen_ = 0, audioBytes_ = 0;
  uint32_t firstAudioMs_ = 0;
};
} // namespace

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
  FramePlayer *player = new FramePlayer(t1); // ~8 KB prebuffer; keep it off the loop task's stack
  net::Response res = net::apiStream("POST", "/api/device/talk-stream", rec, len, "application/octet-stream", *player);
  free(rec);
  player->finish();
  hw::setAll(0, 0, 0);

  bool ok = res.status == 200 && player->gotEnd() && !player->hadError();
  Serial.printf("EVT talk: %s status=%d, first audio %lu ms after release, %.1f s of reply, done at %lu ms%s%s\n",
                ok ? "done" : "FAILED", res.status, (unsigned long)player->firstAudioMs(),
                player->audioBytes() / 2.0f / audio::SAMPLE_RATE, (unsigned long)(millis() - t1),
                res.error.length() ? " error=" : "", res.error.c_str());
  if (res.body.length()) Serial.printf("EVT talk: %s\n", res.body.substring(0, 300).c_str());
  delete player;
  if (!ok) {
    hw::setAll(60, 0, 0);
    delay(400);
    hw::setAll(0, 0, 0);
  }
  return ok;
}

} // namespace voice

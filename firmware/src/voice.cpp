#include "voice.h"

#include "audio.h"
#include "diag.h"
#include "hw.h"
#include "net.h"

namespace voice {

namespace {
// Parses the talk-stream / talk-audio frames ([type][u32 LE length][payload]) as they arrive and
// plays audio frames immediately, after a short prebuffer to ride out network jitter.
class FramePlayer : public Stream {
public:
  FramePlayer(uint32_t t0, const std::function<bool()> &interrupt) : t0_(t0), interrupt_(interrupt) {}

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *d, size_t n) override {
    if (interrupted_) return 0;
    if (interrupt_ && millis() - lastCheck_ >= 50) { // I2C read; don't do it on every chunk
      lastCheck_ = millis();
      if (interrupt_()) {
        interrupted_ = true;
        diag::evt("talk: interrupted (%lu ms)", (unsigned long)(millis() - t0_));
        audio::endPlayback();
        return 0; // makes HTTPClient::writeToStream stop reading
      }
    }
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
  bool wasInterrupted() const { return interrupted_; }
  bool hadError() const { return error_; }
  size_t audioBytes() const { return audioBytes_; }
  uint32_t firstAudioMs() const { return firstAudioMs_; }

private:
  static constexpr size_t PREBUF = audio::SAMPLE_RATE * 2 / 4; // 250 ms

  void endFrame() {
    hdrLen_ = 0;
    switch (type_) {
    case 'T': diag::evt("talk: heard \"%s\" (%lu ms)", text_.c_str(), (unsigned long)(millis() - t0_)); break;
    case 'R': diag::evt("talk: reply \"%s\" (%lu ms)", text_.c_str(), (unsigned long)(millis() - t0_)); break;
    case 'E': diag::evt("talk: server error %s", text_.c_str()); error_ = true; break;
    case 'M': diag::evt("talk: model %s", text_.c_str()); break;
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
    diag::evt("talk: speaking (%lu ms)", (unsigned long)(millis() - t0_));
    audio::beginPlayback();
    audio::writePcm(prebuf_, prebufLen_);
  }

  uint32_t t0_;
  std::function<bool()> interrupt_;
  uint32_t lastCheck_ = 0;
  bool interrupted_ = false;
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
bool s_interrupted = false;
} // namespace

bool interrupted() { return s_interrupted; }

bool talk(const std::function<bool()> &keepGoing, const std::function<bool()> &interrupt, const char *path) {
  s_interrupted = false;
  if (!audio::ok()) {
    diag::evt("talk: audio not initialised");
    return false;
  }
  const size_t maxBytes = MAX_RECORD_SECONDS * audio::SAMPLE_RATE * 2;
  uint8_t *rec = (uint8_t *)ps_malloc(maxBytes);
  if (!rec) {
    diag::evt("talk: out of PSRAM");
    return false;
  }

  hw::setAll(60, 0, 0); // red: listening
  diag::evt("talk: listening");
  float rmsL = 0, rmsR = 0;
  uint32_t failsBefore = hw::keyReadFailures();
  size_t len = audio::record(rec, maxBytes, keepGoing, &rmsL, &rmsR);
  diag::evt("talk: recorded %.1f s, ended by %s (mic rms L=%.0f R=%.0f, key read failures %lu)",
            len / 2.0f / audio::SAMPLE_RATE, len >= maxBytes ? "time limit" : "button release", rmsL, rmsR,
            (unsigned long)(hw::keyReadFailures() - failsBefore));

  if (len < audio::SAMPLE_RATE * 2 / 2) { // under half a second: treat as an accidental tap
    free(rec);
    hw::setAll(0, 0, 0);
    diag::evt("talk: too short, ignored");
    return false;
  }

  hw::setAll(0, 0, 60); // blue: thinking
  uint32_t t1 = millis();
  FramePlayer *player = new FramePlayer(t1, interrupt); // ~8 KB prebuffer; keep it off the loop task's stack
  String route = path ? String(path) : net::config().voicePath;
  diag::evt("talk: sending to %s", route.c_str());
  net::Response res = net::apiStream("POST", route, rec, len, "application/octet-stream", *player);
  free(rec);
  player->finish();
  hw::setAll(0, 0, 0);

  if (player->wasInterrupted()) {
    s_interrupted = true;
    delete player;
    return false;
  }
  bool ok = res.status == 200 && player->gotEnd() && !player->hadError();
  diag::evt("talk: %s status=%d, first audio %lu ms after release, %.1f s of reply, done at %lu ms%s%s",
                ok ? "done" : "FAILED", res.status, (unsigned long)player->firstAudioMs(),
                player->audioBytes() / 2.0f / audio::SAMPLE_RATE, (unsigned long)(millis() - t1),
                res.error.length() ? " error=" : "", res.error.c_str());
  if (res.body.length()) diag::evt("talk: %s", res.body.substring(0, 300).c_str());
  delete player;
  if (!ok) {
    hw::setAll(60, 0, 0);
    delay(400);
    hw::setAll(0, 0, 0);
  }
  return ok;
}

} // namespace voice

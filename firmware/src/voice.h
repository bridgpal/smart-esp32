// Push-to-talk: record -> POST /api/device/talk -> play the spoken reply.
#pragma once
#include <Arduino.h>
#include <functional>

namespace voice {

constexpr uint32_t MAX_RECORD_SECONDS = 10;

// Records while keepGoing() is true (capped at MAX_RECORD_SECONDS), then runs the round trip.
// Ring: red = listening, blue = thinking, green = speaking. Prints progress to Serial.
// If interrupt is given and returns true while the reply is streaming, the reply is cut off
// (barge-in) and talk() returns false; interrupted() then reports true.
// path overrides the configured route (net::config().voicePath) for this call.
bool talk(const std::function<bool()> &keepGoing, const std::function<bool()> &interrupt = nullptr,
          const char *path = nullptr);
bool interrupted();

} // namespace voice

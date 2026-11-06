// Push-to-talk: record -> POST /api/device/talk -> play the spoken reply.
#pragma once
#include <Arduino.h>
#include <functional>

namespace voice {

constexpr uint32_t MAX_RECORD_SECONDS = 10;

// Records while keepGoing() is true (capped at MAX_RECORD_SECONDS), then runs the round trip.
// Ring: red = listening, blue = thinking, green = speaking. Prints progress to Serial.
bool talk(const std::function<bool()> &keepGoing);

} // namespace voice

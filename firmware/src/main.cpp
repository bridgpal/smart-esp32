// smart-esp32 starter firmware.
// Boots the board, joins Wi-Fi, exposes a serial console (see console.cpp), and posts
// key presses to the Netlify API. The ring color in the API response is shown on the LEDs.
#ifndef PIO_UNIT_TESTING

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "audio.h"
#include "diag.h"
#include "console.h"
#include "hw.h"
#include "net.h"
#include "voice.h"

namespace {
bool s_wasConnected = false;

void onKeyPress(uint8_t key) {
  diag::evt("key %s", hw::keyName(key));
  hw::setAll(0, 0, 40);
  if (!net::connected() || !net::config().apiBase.length()) {
    delay(80);
    hw::setAll(0, 0, 0);
    return;
  }

  JsonDocument doc;
  doc["type"] = "key";
  doc["key"] = hw::keyName(key);
  doc["uptimeMs"] = millis();
  String body;
  serializeJson(doc, body);

  net::Response r = net::api("POST", "/api/device/event", body);
  diag::evt("api status=%d %s", r.status, r.error.c_str());

  JsonDocument reply;
  if (r.ok() && !deserializeJson(reply, r.body) && reply["led"].is<JsonArrayConst>()) {
    JsonArrayConst led = reply["led"];
    hw::setAll(led[0] | 0, led[1] | 0, led[2] | 0);
  } else {
    hw::setAll(r.ok() ? 0 : 60, 0, 0); // red flash on failure
    delay(300);
    hw::setAll(0, 0, 0);
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 1500) delay(10); // give the USB console a moment

  bool hwOk = hw::begin();
  bool audioOk = audio::begin();
  hw::spin(0, 40, 80);
  net::loadConfig();
  net::begin();

  Serial.printf("\nsmart-esp32 %s on %s, expander %s, audio %s. Hold K1 (rightmost) to talk. Type 'help'.\n", FW_VERSION,
                net::deviceId().c_str(), hwOk ? "ok" : "MISSING", audioOk ? "ok" : "FAILED");
}

void loop() {
  console::poll();

  uint8_t pressed = hw::pollKeyPresses();
  for (uint8_t k = 0; k < hw::KEY_COUNT; k++) {
    if (!(pressed & (1 << k))) continue;
    if (k == hw::KEY_9) { // K1 (rightmost): hold to talk; pressing it during a reply interrupts and listens again
      diag::evt("key K1 (talk)");
      auto talkDown = [] { return (bool)((hw::readKeys() >> hw::KEY_9) & 1); };
      while (!voice::talk(talkDown, talkDown) && voice::interrupted() && talkDown()) {
        diag::evt("key K1 (talk again)");
      }
      hw::resyncKeys();
    } else {
      onKeyPress(k);
    }
  }

  bool now = net::connected();
  if (now != s_wasConnected) {
    s_wasConnected = now;
    if (now) diag::evt("wifi up %s", WiFi.localIP().toString().c_str());
    else diag::evt("wifi down");
  }
  delay(5);
}

#endif

#include "console.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_chip_info.h>

#include "audio.h"
#include "hw.h"
#include "net.h"
#include "voice.h"

namespace console {

namespace {
String s_buf;

void ok() { Serial.println("OK"); }
void err(const String &why) { Serial.println("ERR " + why); }

String nextToken(String &rest) {
  rest.trim();
  int sp = rest.indexOf(' ');
  String tok = sp < 0 ? rest : rest.substring(0, sp);
  rest = sp < 0 ? "" : rest.substring(sp + 1);
  return tok;
}

String mask(const String &s) {
  if (!s.length()) return "(unset)";
  if (s.length() <= 4) return "****";
  return s.substring(0, 2) + "****" + s.substring(s.length() - 2);
}

void printResponse(const net::Response &r, size_t maxBody = 1024) {
  if (r.error.length()) Serial.printf("error: %s\n", r.error.c_str());
  Serial.printf("status: %d\n", r.status);
  if (r.body.length()) {
    Serial.println(r.body.substring(0, maxBody));
    if (r.body.length() > maxBody) Serial.printf("... (%u bytes total)\n", (unsigned)r.body.length());
  }
}

void cmdHelp() {
  Serial.print(
      "commands:\n"
      "  info                      chip, memory, firmware, wifi\n"
      "  scan                      I2C bus scan\n"
      "  keys                      current key state\n"
      "  led <r> <g> <b> | off | spin\n"
      "  amp on|off                speaker power amp (EXIO8)\n"
      "  wifi                      status;  wifi scan  lists networks\n"
      "  config show | set <key> <value> | clear\n"
      "                            keys: wifi.ssid wifi.pass api.base api.token\n"
      "  ping                      GET  {api.base}/api/device/ping\n"
      "  event <type> [key]        POST {api.base}/api/device/event\n"
      "  get <https-url>           HTTPS GET any URL\n"
      "  tone [hz] [ms]            speaker beep (default 440 Hz, 500 ms)\n"
      "  vol [0-100]               show or set speaker volume\n"
      "  rec [seconds]             record from the mics, print levels (default 3 s)\n"
      "  playback                  play the last `rec` recording\n"
      "  say <text>                TTS via {api.base}/api/device/say and play it\n"
      "  talk [seconds]            record, then full voice round trip via /api/device/talk\n"
      "  reboot\n");
  ok();
}

void cmdInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  Serial.printf("firmware: smart-esp32 %s (%s %s)\n", FW_VERSION, __DATE__, __TIME__);
  Serial.printf("chip: %s rev %d, %d cores, %lu MHz\n", ESP.getChipModel(), chip.revision, chip.cores,
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("flash: %lu MB, psram: %lu KB (free %lu KB), heap free: %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() >> 20), (unsigned long)(ESP.getPsramSize() >> 10),
                (unsigned long)(ESP.getFreePsram() >> 10), (unsigned long)(ESP.getFreeHeap() >> 10));
  Serial.printf("device: %s  mac: %s  uptime: %lus\n", net::deviceId().c_str(), WiFi.macAddress().c_str(),
                (unsigned long)(millis() / 1000));
  Serial.printf("expander: %s\n", hw::expanderOk() ? "ok" : "MISSING");
  if (net::connected()) {
    Serial.printf("wifi: connected to %s, ip %s, rssi %d dBm\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("wifi: not connected (ssid %s)\n", net::config().wifiSsid.length() ? net::config().wifiSsid.c_str() : "unset");
  }
  ok();
}

void cmdScan() {
  uint8_t found[32];
  int n = hw::scanI2C(found, 32);
  for (int i = 0; i < n; i++) {
    const char *name = "";
    switch (found[i]) {
    case 0x18: name = "ES8311 codec"; break;
    case 0x20: name = "TCA9555 expander"; break;
    case 0x40: name = "ES7210 mic ADC"; break;
    case 0x51: name = "PCF85063 RTC"; break;
    }
    Serial.printf("0x%02x %s\n", found[i], name);
  }
  Serial.printf("%d device(s)\n", n);
  ok();
}

void cmdKeys() {
  uint8_t k = hw::readKeys();
  for (uint8_t i = 0; i < hw::KEY_COUNT; i++) Serial.printf("%s=%s ", hw::keyName(i), (k >> i) & 1 ? "down" : "up");
  Serial.println();
  ok();
}

void cmdLed(String rest) {
  String a = nextToken(rest);
  if (a == "off") {
    hw::setAll(0, 0, 0);
  } else if (a == "spin") {
    hw::spin(0, 40, 80, 2);
  } else {
    String b = nextToken(rest), c = nextToken(rest);
    if (!a.length() || !b.length() || !c.length()) return err("usage: led <r> <g> <b> | off | spin");
    hw::setAll(a.toInt(), b.toInt(), c.toInt());
  }
  ok();
}

void cmdAmp(String rest) {
  String a = nextToken(rest);
  if (a != "on" && a != "off") return err("usage: amp on|off");
  if (!hw::setAmp(a == "on")) return err("expander write failed");
  ok();
}

void cmdWifi(String rest) {
  if (nextToken(rest) == "scan") {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++)
      Serial.printf("%4d dBm  ch%-2d  %s\n", WiFi.RSSI(i), WiFi.channel(i), WiFi.SSID(i).c_str());
    WiFi.scanDelete();
    Serial.printf("%d network(s)\n", n);
    return ok();
  }
  Serial.printf("status: %s\n", net::connected() ? "connected" : "disconnected");
  if (net::connected())
    Serial.printf("ssid: %s  ip: %s  rssi: %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  ok();
}

void cmdConfig(String rest) {
  String sub = nextToken(rest);
  auto &c = net::config();
  if (sub == "show" || sub == "") {
    Serial.printf("wifi.ssid = %s\n", c.wifiSsid.length() ? c.wifiSsid.c_str() : "(unset)");
    Serial.printf("wifi.pass = %s\n", mask(c.wifiPass).c_str());
    Serial.printf("api.base  = %s\n", c.apiBase.length() ? c.apiBase.c_str() : "(unset)");
    Serial.printf("api.token = %s\n", mask(c.apiToken).c_str());
    return ok();
  }
  if (sub == "clear") {
    net::clearConfig();
    return ok();
  }
  if (sub == "set") {
    String key = nextToken(rest);
    rest.trim(); // value may contain spaces (SSIDs)
    if (!net::setConfig(key, rest)) return err("unknown key: " + key);
    if (key.startsWith("wifi.")) {
      WiFi.disconnect();
      net::begin();
      Serial.println("wifi restarting with new credentials");
    }
    return ok();
  }
  err("usage: config show | set <key> <value> | clear");
}

void cmdPing() {
  net::Response r = net::api("GET", "/api/device/ping");
  printResponse(r);
  r.ok() ? ok() : err("ping failed");
}

void cmdEvent(String rest) {
  String type = nextToken(rest);
  if (!type.length()) return err("usage: event <type> [key]");
  String key = nextToken(rest);
  String body = "{\"type\":\"" + type + "\"" + (key.length() ? ",\"key\":\"" + key + "\"" : String()) + "}";
  net::Response r = net::api("POST", "/api/device/event", body);
  printResponse(r);
  r.ok() ? ok() : err("event failed");
}

void cmdTone(String rest) {
  if (!audio::ok()) return err("audio not initialised");
  String hz = nextToken(rest), ms = nextToken(rest);
  audio::tone(hz.length() ? hz.toInt() : 440, ms.length() ? ms.toInt() : 500);
  ok();
}

void cmdVol(String rest) {
  String v = nextToken(rest);
  if (v.length() && !audio::setVolume(v.toInt())) return err("codec write failed");
  Serial.printf("volume: %d\n", audio::volume());
  ok();
}

void cmdSay(String rest) {
  rest.trim();
  if (!rest.length()) return err("usage: say <text>");
  if (!audio::ok()) return err("audio not initialised");
  JsonDocument doc;
  doc["text"] = rest;
  String body;
  serializeJson(doc, body);

  uint32_t t0 = millis();
  net::Binary b = net::apiBinary("POST", "/api/device/say", (const uint8_t *)body.c_str(), body.length(),
                                 "application/json", 4 * 1024 * 1024); // ~2 min of 16 kHz audio
  uint32_t t1 = millis();
  if (!b.data) {
    printResponse(b.res, 512);
    return err("say failed");
  }
  Serial.printf("got %u bytes (%.1f s of audio) in %lu ms%s%s\n", (unsigned)b.len,
                b.len / 2.0f / audio::SAMPLE_RATE, (unsigned long)(t1 - t0), b.res.error.length() ? ", " : "",
                b.res.error.c_str());
  audio::playPcm16(b.data, b.len);
  free(b.data);
  ok();
}

uint8_t *s_lastRec = nullptr;
size_t s_lastRecLen = 0;

void cmdRec(String rest) {
  if (!audio::ok()) return err("audio not initialised");
  String a = nextToken(rest);
  float secs = a.length() ? a.toFloat() : 3;
  if (secs <= 0 || secs > voice::MAX_RECORD_SECONDS) return err("seconds must be 0-10");
  if (!s_lastRec) s_lastRec = (uint8_t *)ps_malloc(voice::MAX_RECORD_SECONDS * audio::SAMPLE_RATE * 2);
  if (!s_lastRec) return err("out of PSRAM");
  size_t want = (size_t)(secs * audio::SAMPLE_RATE) * 2;
  float l = 0, r = 0;
  hw::setAll(60, 0, 0);
  s_lastRecLen = audio::record(s_lastRec, want, [] { return true; }, &l, &r);
  hw::setAll(0, 0, 0);
  int16_t peak = 0;
  for (size_t i = 0; i < s_lastRecLen / 2; i++) peak = max(peak, (int16_t)abs(((int16_t *)s_lastRec)[i]));
  Serial.printf("recorded %u bytes; rms L(mic1)=%.0f R(mic2)=%.0f; peak(mic1)=%d\n", (unsigned)s_lastRecLen, l, r, peak);
  ok();
}

void cmdPlayback() {
  if (!s_lastRecLen) return err("nothing recorded yet (rec first)");
  audio::playPcm16(s_lastRec, s_lastRecLen);
  ok();
}

void cmdTalk(String rest) {
  String a = nextToken(rest);
  float secs = a.length() ? a.toFloat() : 4;
  uint32_t until = millis() + (uint32_t)(secs * 1000);
  voice::talk([until] { return (int32_t)(millis() - until) < 0; }) ? ok() : err("talk failed");
}

void cmdGet(String rest) {
  String url = nextToken(rest);
  if (!url.length()) return err("usage: get <https-url>");
  net::Response r = net::request("GET", url);
  printResponse(r, 512);
  r.ok() ? ok() : err("request failed");
}
} // namespace

void run(String line) {
  line.trim();
  if (!line.length()) return;
  String rest = line;
  String cmd = nextToken(rest);
  cmd.toLowerCase();
  if (cmd == "help" || cmd == "?") cmdHelp();
  else if (cmd == "info") cmdInfo();
  else if (cmd == "scan") cmdScan();
  else if (cmd == "keys") cmdKeys();
  else if (cmd == "led") cmdLed(rest);
  else if (cmd == "amp") cmdAmp(rest);
  else if (cmd == "wifi") cmdWifi(rest);
  else if (cmd == "config") cmdConfig(rest);
  else if (cmd == "ping") cmdPing();
  else if (cmd == "event") cmdEvent(rest);
  else if (cmd == "get") cmdGet(rest);
  else if (cmd == "tone") cmdTone(rest);
  else if (cmd == "vol") cmdVol(rest);
  else if (cmd == "rec") cmdRec(rest);
  else if (cmd == "playback") cmdPlayback();
  else if (cmd == "say") cmdSay(rest);
  else if (cmd == "talk") cmdTalk(rest);
  else if (cmd == "reboot") {
    ok();
    Serial.flush();
    delay(100);
    ESP.restart();
  } else err("unknown command: " + cmd + " (try help)");
}

void poll() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      String line = s_buf;
      s_buf = "";
      run(line);
    } else if (s_buf.length() < 512) {
      s_buf += ch;
    }
  }
}

} // namespace console

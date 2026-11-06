#include "net.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>

// Compile-time defaults come from the repo-root .env via load_env.py (never committed).
// Values saved on the device with `config set` (NVS) win over these.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef API_BASE
#define API_BASE ""
#endif
#ifndef API_TOKEN
#define API_TOKEN ""
#endif

namespace net {

namespace {
Config s_cfg;
constexpr const char *NS = "cfg";

struct KeyMap {
  const char *key;
  const char *nvs;
  String Config::*field;
};
const KeyMap KEYS[] = {
    {"wifi.ssid", "wifi_ssid", &Config::wifiSsid},
    {"wifi.pass", "wifi_pass", &Config::wifiPass},
    {"api.base", "api_base", &Config::apiBase},
    {"api.token", "api_token", &Config::apiToken},
    {"voice.path", "voice_path", &Config::voicePath},
};
} // namespace

Config &config() { return s_cfg; }

void loadConfig() {
  s_cfg = {WIFI_SSID, WIFI_PASS, API_BASE, API_TOKEN, "/api/device/talk-audio"};
  Preferences p;
  if (!p.begin(NS, false)) return; // read-write so the namespace is created on first boot
  for (const auto &k : KEYS) {
    if (p.isKey(k.nvs)) s_cfg.*(k.field) = p.getString(k.nvs);
  }
  p.end();
  while (s_cfg.apiBase.endsWith("/")) s_cfg.apiBase.remove(s_cfg.apiBase.length() - 1);
}

bool setConfig(const String &key, const String &value) {
  for (const auto &k : KEYS) {
    if (key != k.key) continue;
    Preferences p;
    p.begin(NS, false);
    p.putString(k.nvs, value);
    p.end();
    loadConfig();
    return true;
  }
  return false;
}

void clearConfig() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
  loadConfig();
}

String deviceId() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // factory MAC from eFuse; valid before Wi-Fi starts
  char buf[24];
  snprintf(buf, sizeof(buf), "esp32s3-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return buf;
}

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(deviceId().c_str());
  WiFi.setAutoReconnect(true);
  if (s_cfg.wifiSsid.length()) WiFi.begin(s_cfg.wifiSsid.c_str(), s_cfg.wifiPass.c_str());
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!connected() && millis() - start < timeoutMs) delay(100);
  return connected();
}

Response request(const char *method, const String &url, const String &jsonBody, bool withAuth) {
  Response res;
  if (!connected()) {
    res.error = "wifi not connected";
    return res;
  }
  if (!url.startsWith("https://")) {
    res.error = "only https:// URLs are allowed";
    return res;
  }

  NetworkClientSecure client;
  client.useBuiltinCACertBundle(); // Mozilla root bundle compiled into the core

  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    res.error = "bad url";
    return res;
  }
  http.setUserAgent("smart-esp32/" FW_VERSION);
  http.addHeader("Accept", "application/json");
  http.addHeader("X-Device-Id", deviceId());
  if (withAuth && s_cfg.apiToken.length()) http.addHeader("Authorization", "Bearer " + s_cfg.apiToken);
  if (jsonBody.length()) http.addHeader("Content-Type", "application/json");

  res.status = http.sendRequest(method, jsonBody);
  if (res.status > 0) {
    res.body = http.getString();
  } else {
    res.error = HTTPClient::errorToString(res.status);
  }
  http.end();
  return res;
}

namespace {
// Stream sink that appends into a fixed PSRAM buffer (HTTPClient::writeToStream decodes chunking).
class BufferSink : public Stream {
public:
  BufferSink(uint8_t *buf, size_t cap) : buf_(buf), cap_(cap) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *d, size_t n) override {
    if (len_ + n > cap_) {
      overflow_ = true;
      n = cap_ - len_;
    }
    memcpy(buf_ + len_, d, n);
    len_ += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t len() const { return len_; }
  bool overflow() const { return overflow_; }

private:
  uint8_t *buf_;
  size_t cap_, len_ = 0;
  bool overflow_ = false;
};
} // namespace

String urlDecode(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '%' && i + 2 < in.length()) {
      out += (char)strtol(in.substring(i + 1, i + 3).c_str(), nullptr, 16);
      i += 2;
    } else {
      out += c == '+' ? ' ' : c;
    }
  }
  return out;
}

Binary apiBinary(const char *method, const String &path, const uint8_t *body, size_t bodyLen,
                 const char *contentType, size_t maxBytes) {
  Binary out;
  Response &res = out.res;
  if (!s_cfg.apiBase.length()) {
    res.error = "api.base not set";
    return out;
  }
  if (!connected()) {
    res.error = "wifi not connected";
    return out;
  }

  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(20000);
  http.setConnectTimeout(10000);
  if (!http.begin(client, s_cfg.apiBase + path)) {
    res.error = "bad url";
    return out;
  }
  http.setUserAgent("smart-esp32/" FW_VERSION);
  http.addHeader("X-Device-Id", deviceId());
  http.addHeader("Authorization", "Bearer " + s_cfg.apiToken);
  if (bodyLen) http.addHeader("Content-Type", contentType);
  const char *want[] = {"X-Transcript", "X-Reply"};
  http.collectHeaders(want, 2);

  res.status = http.sendRequest(method, (uint8_t *)body, bodyLen);
  out.transcript = urlDecode(http.header("X-Transcript"));
  out.reply = urlDecode(http.header("X-Reply"));
  if (res.status <= 0) {
    res.error = HTTPClient::errorToString(res.status);
  } else if (res.status != 200) {
    res.body = http.getString();
  } else {
    uint8_t *buf = (uint8_t *)ps_malloc(maxBytes);
    if (!buf) {
      res.error = "out of PSRAM";
    } else {
      BufferSink sink(buf, maxBytes);
      int written = http.writeToStream(&sink);
      if (written < 0 && !sink.overflow()) {
        res.error = HTTPClient::errorToString(written);
        free(buf);
      } else {
        out.data = buf;
        out.len = sink.len();
        if (sink.overflow()) res.error = "truncated at " + String(maxBytes) + " bytes";
      }
    }
  }
  http.end();
  return out;
}

Response apiStream(const char *method, const String &path, const uint8_t *body, size_t bodyLen,
                   const char *contentType, Stream &sink) {
  Response res;
  if (!s_cfg.apiBase.length()) {
    res.error = "api.base not set";
    return res;
  }
  if (!connected()) {
    res.error = "wifi not connected";
    return res;
  }
  NetworkClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(20000);
  http.setConnectTimeout(10000);
  if (!http.begin(client, s_cfg.apiBase + path)) {
    res.error = "bad url";
    return res;
  }
  http.setUserAgent("smart-esp32/" FW_VERSION);
  http.addHeader("X-Device-Id", deviceId());
  http.addHeader("Authorization", "Bearer " + s_cfg.apiToken);
  if (bodyLen) http.addHeader("Content-Type", contentType);

  res.status = http.sendRequest(method, (uint8_t *)body, bodyLen);
  if (res.status <= 0) {
    res.error = HTTPClient::errorToString(res.status);
  } else if (res.status != 200) {
    res.body = http.getString();
  } else {
    int written = http.writeToStream(&sink);
    if (written < 0) res.error = HTTPClient::errorToString(written);
  }
  http.end();
  return res;
}

Response api(const char *method, const String &path, const String &jsonBody) {
  if (!s_cfg.apiBase.length()) {
    Response res;
    res.error = "api.base not set (config set api.base https://<site>.netlify.app)";
    return res;
  }
  return request(method, s_cfg.apiBase + path, jsonBody, true);
}

} // namespace net

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
};
} // namespace

Config &config() { return s_cfg; }

void loadConfig() {
  s_cfg = {WIFI_SSID, WIFI_PASS, API_BASE, API_TOKEN};
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

Response api(const char *method, const String &path, const String &jsonBody) {
  if (!s_cfg.apiBase.length()) {
    Response res;
    res.error = "api.base not set (config set api.base https://<site>.netlify.app)";
    return res;
  }
  return request(method, s_cfg.apiBase + path, jsonBody, true);
}

} // namespace net

// Wi-Fi, persisted config (NVS), and an HTTPS JSON client for the Netlify API.
#pragma once
#include <Arduino.h>

namespace net {

struct Config {
  String wifiSsid;
  String wifiPass;
  String apiBase;  // e.g. https://smart-esp32.netlify.app (no trailing slash)
  String apiToken; // sent as Authorization: Bearer <token>
};

// Keys accepted by `config set`: wifi.ssid, wifi.pass, api.base, api.token
Config &config();
void loadConfig();
bool setConfig(const String &key, const String &value);
void clearConfig();

String deviceId(); // stable, derived from the MAC

void begin();       // start Wi-Fi if an SSID is configured
bool connected();
bool waitConnected(uint32_t timeoutMs);

struct Response {
  int status = 0; // HTTP status, or negative HTTPClient error code
  String body;
  String error;
  bool ok() const { return status >= 200 && status < 300; }
};

// Full-URL request (any HTTPS host, validated against the built-in CA bundle).
Response request(const char *method, const String &url, const String &jsonBody = "",
                 bool withAuth = false);
// Request against config().apiBase with the device token and X-Device-Id header.
Response api(const char *method, const String &path, const String &jsonBody = "");

} // namespace net

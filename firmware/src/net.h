// Wi-Fi, persisted config (NVS), and an HTTPS JSON client for the Netlify API.
#pragma once
#include <Arduino.h>

namespace net {

struct Config {
  String wifiSsid;
  String wifiPass;
  String apiBase;  // e.g. https://smart-esp32.netlify.app (no trailing slash)
  String apiToken; // sent as Authorization: Bearer <token>
  String voicePath; // push-to-talk route, default /api/device/talk-audio
};

// Keys accepted by `config set`: wifi.ssid, wifi.pass, api.base, api.token, voice.path
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

// Binary download from the API into a PSRAM buffer. On success data/len hold the body and the
// caller must free(data). On HTTP errors the (JSON) error body is in res.body and data is null.
struct Binary {
  Response res;
  uint8_t *data = nullptr;
  size_t len = 0;
  String transcript, reply; // X-Transcript / X-Reply response headers (URL-decoded), if present
};
// body may be JSON (contentType "application/json") or raw bytes ("application/octet-stream").
Binary apiBinary(const char *method, const String &path, const uint8_t *body, size_t bodyLen,
                 const char *contentType, size_t maxBytes);

// Streams the response body into sink as it arrives (chunked encoding decoded). On a non-200
// status the body is returned in res.body instead.
Response apiStream(const char *method, const String &path, const uint8_t *body, size_t bodyLen,
                   const char *contentType, Stream &sink);

} // namespace net

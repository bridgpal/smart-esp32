// Optional compile-time defaults. Copy to include/secrets.h (gitignored) and fill in.
// You can skip this file entirely and set values at runtime over serial instead:
//   make cmd C="config set wifi.ssid MyNetwork"
// Runtime values are stored in NVS and override these.
#pragma once

#define WIFI_SSID ""
#define WIFI_PASS ""
#define API_BASE "" // e.g. "https://smart-esp32.netlify.app"
#define API_TOKEN "" // must match DEVICE_TOKEN on the Netlify site

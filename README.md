# smart-esp32

<img src="docs/device.jpg" alt="SMKTelec ESP32-S3 AI Smart Speaker board in its enclosure, with the separate speaker module" width="420">

**Device:** [SMKTelec ESP32-S3 AI Smart Speaker Development Board (Amazon B0GWQDCHK8)](https://www.amazon.com/dp/B0GWQDCHK8),
a rebrand of the [Waveshare ESP32-S3-AUDIO-Board](https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board).

TL;DR: two halves in one repo. `firmware/` runs on the SMKTelec "ESP32-S3 AI Smart Speaker"
board (a Waveshare ESP32-S3-AUDIO-Board clone). `site/` is the Netlify site whose functions the
board calls. Secrets live in a gitignored `.env` at the repo root, shared by both.

```
.env.example   template for .env (Wi-Fi, API base URL, DEVICE_TOKEN)
firmware/      PlatformIO project, Makefile, dev scripts
netlify.toml   Netlify config (base = site)
site/          Netlify site: functions, static files, package.json
```

## Secrets

`cp .env.example .env`, then fill it in. `.env` is gitignored.

| Key | Used by |
|---|---|
| `WIFI_SSID`, `WIFI_PASS` | firmware, baked in at build time |
| `API_BASE` | firmware, e.g. `https://<site>.netlify.app` |
| `DEVICE_TOKEN` | firmware (sent as `Authorization: Bearer`) and the Netlify site (env var of the same name) |

`firmware/load_env.py` passes `.env` into the build. Values set on the device with
`config set` (stored in NVS) override the baked-in ones.

## Hardware (verified on this unit)

| | |
|---|---|
| SoC | ESP32-S3 (QFN56) rev v0.2, dual core 240 MHz, Wi-Fi + BLE 5 |
| Memory | 16 MB quad flash, 8 MB embedded octal PSRAM |
| USB | native USB-Serial/JTAG (`/dev/cu.usbmodem*`, no driver needed) |
| Audio | ES8311 codec + speaker amp, ES7210 mic ADC with dual mic array |
| Buttons | right to left: K1, K2, K3 (on the IO expander), BOOT (GPIO0), RESET (hardware reset, not readable) |
| Other | 7x RGB ring (GPIO38, RGB byte order), TCA9555 IO expander, PCF85063 RTC, TF slot, LCD + camera FPC connectors |

Pin map: [`firmware/include/board.h`](firmware/include/board.h). Source is Waveshare's factory
demo plus checks on this unit (`make cmd C=scan`). See also the
[Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board).

## Firmware (`firmware/`)

```
include/board.h   pin map
src/hw.*          I2C, IO expander, keys, LED ring
src/net.*         Wi-Fi, NVS config, HTTPS JSON client (built-in CA bundle)
src/console.*     serial command console
test/             on-device Unity tests
load_env.py       reads ../.env into the build
scripts/          devcmd.py (drive the console), wipe.sh (clean slate)
```

Run these from `firmware/`:

```sh
make setup                 # .venv with PlatformIO + esptool
make flash                 # build + upload
make monitor               # interactive serial console, type `help`
make cmd C=info            # one-shot commands, exits non-zero on ERR
make cmd C="'led 0 40 0' scan keys"
make listen                # watch key-press events for 30s
make test                  # on-device hardware tests (then `make flash` to restore the app)
make wipe                  # clean slate: eFuse check + full erase + verify every byte is blank
```

If upload can't connect: hold BOOT, tap RESET, release BOOT, then `make flash` again.

## Netlify site (`site/`)

The root `netlify.toml` sets base directory `site`, so linking the repo needs no UI settings.
Commits that don't touch `site/` skip the Netlify build. Set `DEVICE_TOKEN` on the site to the
value in your `.env`.

| Endpoint | Returns |
|---|---|
| `GET /api/device/ping` | `{ ok, deviceId, serverTime, deployId, clientIp }` |
| `POST /api/device/event` `{type, key}` | `{ ok, received, led: [r,g,b] }` |

`cd site && npm install && npm run typecheck`.

## Next steps

- Audio: ES8311 playback and ES7210 capture over I2S (pins in `board.h`, amp enable is EXIO8).
- OTA updates from a Netlify-hosted firmware binary.

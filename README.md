# smart-esp32

TL;DR: firmware + a Netlify device API for the SMKTelec "ESP32-S3 AI Smart Speaker" board
(a Waveshare ESP32-S3-AUDIO-Board clone). `make setup && make flash && make cmd C=info`.
Key presses on the board POST to a Netlify Function, and the color it returns lights the LED ring.

## Hardware (verified on this unit with esptool)

| | |
|---|---|
| SoC | ESP32-S3 (QFN56) rev v0.2, dual core 240 MHz, Wi-Fi + BLE 5 |
| Memory | 16 MB quad flash, 8 MB embedded octal PSRAM |
| USB | native USB-Serial/JTAG (shows up as `/dev/cu.usbmodem*`, no driver needed) |
| Audio | ES8311 codec + speaker amp, ES7210 mic ADC with dual mic array |
| Buttons | right to left: K1, K2, K3 (on the IO expander), BOOT (GPIO0), RESET (hardware reset, not readable) |
| Other | 7x RGB ring (GPIO38, RGB byte order), TCA9555 IO expander (amp enable, 3 keys), PCF85063 RTC, TF slot, LCD + camera FPC connectors |

Pin map lives in [`firmware/include/board.h`](firmware/include/board.h). Source: Waveshare's
factory demo, so double-check anything new against `make cmd C=scan` and the
[Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board).

## Layout

```
firmware/          PlatformIO project (Arduino core 3.3.12 on ESP-IDF 5.5 via pioarduino)
  include/board.h  pin map
  src/hw.*         I2C, IO expander, keys, LED ring
  src/net.*        Wi-Fi, NVS config, HTTPS JSON client (built-in CA bundle)
  src/console.*    serial command console
  test/            on-device Unity tests
cloud/functions/   Netlify Functions the device calls (/api/device/*)
scripts/devcmd.py  send console commands from the terminal (used by `make cmd`)
```

## Dev loop

```sh
make setup                 # .venv with PlatformIO + esptool (first build also downloads the toolchain)
make flash                 # build + upload
make monitor               # interactive serial console, type `help`
make cmd C=info            # one-shot commands, exits non-zero on ERR
make cmd C="'led 0 40 0' scan keys"
make listen                # watch key-press events for 30s
make test                  # on-device hardware tests (then `make flash` to restore the app)
make erase                 # wipe flash incl. saved Wi-Fi/API config
make wipe                  # clean slate: eFuse check + full erase + verify every byte is blank
```

If upload fails to connect: hold BOOT, tap RESET, release BOOT, then `make flash` again.

## Configure Wi-Fi and the API

Stored on the device in NVS, so no secrets in the repo:

```sh
make cmd C="'config set wifi.ssid MyNetwork' 'config set wifi.pass hunter2'"
make cmd C="'config set api.base https://<site>.netlify.app' 'config set api.token <DEVICE_TOKEN>'"
make cmd C="wifi ping"
```

Or copy `firmware/include/secrets.example.h` to `secrets.h` for compile-time defaults.

## Netlify side

`netlify.toml` publishes `cloud/public` and deploys `cloud/functions`. The site needs a
`DEVICE_TOKEN` env var; the device sends it as `Authorization: Bearer`.

| Endpoint | Returns |
|---|---|
| `GET /api/device/ping` | `{ ok, deviceId, serverTime, deployId, clientIp }` |
| `POST /api/device/event` `{type, key}` | `{ ok, received, led: [r,g,b] }` |

`npm run typecheck` checks the functions.

## Next steps

- Audio: ES8311 playback and ES7210 capture over I2S (pins in `board.h`, amp enable is EXIO8).
- OTA updates from a Netlify-hosted firmware binary.

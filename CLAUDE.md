# smart-esp32

ESP32-S3 audio board firmware (PlatformIO, Arduino core 3.x) plus the Netlify device API it calls.
README.md has the hardware table, layout, and dev loop.

## Working on the board
- The board is on USB as `/dev/cu.usbmodem*`. Drive it with `make cmd C="..."`: every console
  command ends with `OK` or `ERR ...`, exit code follows. Use this to verify firmware changes on
  real hardware, not just builds.
- Tools come from `.venv` (`make setup`). Homebrew is at /opt/homebrew/bin and may not be on PATH.
- `make test` leaves the test image on the board; run `make flash` afterwards.
- Pins: `firmware/include/board.h` only. New peripherals go through the TCA9555 when the pin says EXIO.
- New console commands: add to `console.cpp` and the `help` text, end with `ok()` or `err()`.
- Never commit Wi-Fi credentials or tokens. Runtime config lives in NVS; `secrets.h` is gitignored.

## Netlify
- Functions in `cloud/functions/` (modern `export default` + `config.path`). Run `npm run typecheck`.
- No local dev servers. Verify against a deploy preview; prod deploys only via merge to main.
- `DEVICE_TOKEN` env var on the site must match the device's `api.token`.

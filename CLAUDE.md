# smart-esp32

Two halves: `firmware/` (PlatformIO, Arduino core 3.x, ESP32-S3 audio board) and `site/` (Netlify
site the board calls). README.md has the hardware table and both dev loops.

## Secrets
- All secrets live in the repo-root `.env` (gitignored, mode 600). Template: `.env.example`.
- Never commit secrets, echo them into chat, or print `.env` contents. Mask values when showing config.
- `DEVICE_TOKEN` must match between `.env` (baked into firmware) and the Netlify site env var.

## Firmware (run make from `firmware/`)
- The board is `/dev/cu.usbmodem*`. Drive it with `make cmd C="..."`: every console command ends
  with `OK` or `ERR ...`, exit code follows. Verify firmware changes on the board, not just builds.
- Tools come from `firmware/.venv` (`make setup`). Homebrew is at /opt/homebrew/bin, may not be on PATH.
- `make test` leaves the test image on the board; run `make flash` afterwards.
- Pins: `firmware/include/board.h` only. Pins marked EXIO go through the TCA9555.
- New console commands: add to `console.cpp` and the `help` text, end with `ok()` or `err()`.

## Site (run npm from `site/`)
- Functions in `site/netlify/functions/` (modern `export default` + `config.path`). `npm run typecheck`.
- Netlify base directory is `site`. No local dev servers; verify against a deploy preview.
- Production deploys only via merge to main.

#!/usr/bin/env bash
# Return the board to a known-clean state, removing everything a previous firmware left behind.
#   1. eFuse security summary (read-only; eFuses are one-time-programmable and can't be erased)
#   2. Erase all of flash (bootloader, partition table, app, NVS, OTA slots, filesystem)
#   3. Read the whole flash back and confirm every byte is 0xFF
# The ESP32-S3 ROM bootloader is mask ROM, so no firmware can alter it.
set -euo pipefail

PORT="${1:?usage: scripts/wipe.sh <port>}"
VENV="$(cd "$(dirname "$0")/.." && pwd)/.venv/bin"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "== 1/3 eFuse security state (read-only)"
"$VENV/espefuse" --port "$PORT" summary >"$OUT/efuse.txt" 2>&1
grep -E 'SECURE_BOOT|SPI_BOOT_CRYPT_CNT|DIS_DOWNLOAD|DIS_USB|JTAG|KEY_PURPOSE_[0-5] |ENABLE_SECURITY_DOWNLOAD|DIS_DIRECT_BOOT|FLASH_TPUW' "$OUT/efuse.txt" | sed 's/^/   /'

echo "== 2/3 erasing all flash"
"$VENV/esptool" --port "$PORT" erase-flash

echo "== 3/3 reading back all flash to verify it is blank"
SIZE=$("$VENV/esptool" --port "$PORT" flash-id 2>&1 | sed -n 's/.*Detected flash size: \([0-9]*\)MB.*/\1/p')
"$VENV/esptool" --port "$PORT" --baud 921600 read-flash 0 "$((SIZE * 1024 * 1024))" "$OUT/flash.bin"
"$VENV/python" -I - "$OUT/flash.bin" <<'EOF'
import sys
data = open(sys.argv[1], "rb").read()
dirty = len(data) - data.count(0xFF)
if dirty:
    first = next(i for i, b in enumerate(data) if b != 0xFF)
    sys.exit(f"NOT BLANK: {dirty} non-0xFF bytes, first at 0x{first:06x}")
print(f"   verified blank: {len(data):,} bytes, all 0xFF")
EOF
echo "== done. Flash this repo's firmware with: make flash"

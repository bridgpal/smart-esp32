#!/usr/bin/env python3
"""Send console commands to the board and print the replies.

Each firmware command ends with a line "OK" or "ERR ...". Exit code is 0 on OK, 1 on ERR,
2 on timeout. Opens the port without toggling DTR/RTS, so the board keeps running (no reboot).

  scripts/devcmd.py info
  scripts/devcmd.py "led 0 40 0" scan keys
  scripts/devcmd.py --listen 20        # just print serial output (key events) for 20s
"""
import argparse
import glob
import sys
import time

import serial  # pyserial ships with PlatformIO


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no board found (looked for /dev/cu.usbmodem* and /dev/ttyACM*)")
    return ports[0]


def open_port(path):
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.1
    # Leave DTR/RTS as the OS sets them on open (both asserted). USB-Serial/JTAG resets the chip
    # when RTS is high and DTR is low, so lowering DTR first (what pyserial does) would reboot it.
    s.open()
    return s


def run(s, cmd, timeout):
    s.reset_input_buffer()
    s.write((cmd + "\n").encode())
    deadline = time.time() + timeout
    buf = b""
    print(f"> {cmd}")
    while time.time() < deadline:
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").rstrip("\r")
            print(text)
            if text == "OK":
                return 0
            if text.startswith("ERR"):
                return 1
    print(f"(timeout after {timeout}s)")
    return 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("commands", nargs="*")
    ap.add_argument("--port", default=None)
    ap.add_argument("--timeout", type=float, default=20)
    ap.add_argument("--listen", type=float, default=0, help="seconds to print raw output after commands")
    a = ap.parse_args()

    s = open_port(a.port or find_port())
    rc = 0
    for cmd in a.commands:
        rc = max(rc, run(s, cmd, a.timeout))
    if a.listen:
        end = time.time() + a.listen
        while time.time() < end:
            try:
                data = s.read(4096)
            except serial.SerialException:
                # RESET (or a crash) re-enumerates USB; wait for the port to come back.
                print("\n(board disconnected, waiting for it to come back)", flush=True)
                s.close()
                while time.time() < end:
                    time.sleep(0.5)
                    try:
                        s = open_port(a.port or find_port())
                        print("(reconnected)", flush=True)
                        break
                    except (serial.SerialException, SystemExit):
                        continue
                continue
            if data:
                sys.stdout.write(data.decode(errors="replace"))
                sys.stdout.flush()
    s.close()
    sys.exit(rc)


if __name__ == "__main__":
    main()

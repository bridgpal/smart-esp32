VENV := .venv
PIO := $(VENV)/bin/pio
PY := $(VENV)/bin/python
FW := firmware
PORT ?= $(firstword $(wildcard /dev/cu.usbmodem*) $(wildcard /dev/ttyACM*))

.PHONY: setup build flash monitor run test cmd listen erase wipe chip clean ports

setup:              ## create .venv with PlatformIO + esptool
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install -q --upgrade pip
	$(VENV)/bin/pip install -q -r requirements.txt

build:              ## compile firmware
	$(PIO) run -d $(FW)

flash:              ## compile + upload over USB
	$(PIO) run -d $(FW) -t upload --upload-port $(PORT)

monitor:            ## interactive serial monitor (Ctrl+C to quit)
	$(PIO) device monitor -d $(FW) -p $(PORT)

run: flash monitor  ## flash then monitor

test:               ## on-device Unity tests (flashes a test image; run `make flash` after)
	$(PIO) test -d $(FW) --upload-port $(PORT) --test-port $(PORT)

cmd:                ## send console commands: make cmd C="info"  or  C="'led 0 40 0' scan"
	$(PY) scripts/devcmd.py --port $(PORT) $(C)

listen:             ## print serial output for 30s (key events etc.)
	$(PY) scripts/devcmd.py --port $(PORT) --listen 30

chip:               ## esptool chip + flash info
	$(VENV)/bin/esptool --port $(PORT) flash-id

erase:              ## wipe the whole flash, including saved config
	$(VENV)/bin/esptool --port $(PORT) erase-flash

wipe:               ## clean slate: eFuse security check, full erase, read back and verify blank
	scripts/wipe.sh $(PORT)

ports:
	@echo $(or $(PORT),no board found)

clean:
	$(PIO) run -d $(FW) -t clean

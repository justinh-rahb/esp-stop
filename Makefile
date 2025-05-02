# Makefile for PlatformIO ESP8266 E-Stop project

# Default serial port and environment
PORT ?= /dev/ttyUSB0
ENV ?= esp8266

# PlatformIO command
PIO := platformio

# Targets
all: build

build:
	@echo "Building firmware..."
	$(PIO) run -e $(ENV)

upload: build
	@echo "Flashing firmware to $(PORT)..."
	$(PIO) run -e $(ENV) -t upload --upload-port $(PORT)

monitor:
	@echo "Opening serial monitor on $(PORT)..."
	$(PIO) device monitor -e $(ENV) --port $(PORT)

clean:
	@echo "Cleaning project..."
	$(PIO) run -e $(ENV) -t clean

wipe:
	@echo "Erasing EEPROM (you must flash code that supports it)..."
	$(PIO) run -e $(ENV) -t erase

help:
	@echo ""
	@echo "ESP8266 E-Stop Makefile for PlatformIO"
	@echo ""
	@echo "Targets:"
	@echo "  make build      - Compile firmware"
	@echo "  make upload     - Upload firmware to ESP (PORT=/dev/ttyUSB0)"
	@echo "  make monitor    - Open serial monitor"
	@echo "  make clean      - Clean build"
	@echo "  make wipe       - (Optional) EEPROM wipe if supported"
	@echo "  make help       - Show this message"
	@echo ""

.PHONY: all build upload monitor clean help wipe

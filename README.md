# 🔴 ESP-Stop Button for 3D Printers

This project implements a simple emergency stop (E-Stop) button using an ESP8266 and a physical button.

When pressed, it sends a command to a server, smart device, or 3D printer, depending on the selected mode. Supported integrations include:

- **OctoPrint** (3D printer)
- **Moonraker / Klipper** (3D printer)
- **TP-Link Kasa** (local LAN smart plugs/switches)

> [!NOTE]
> Your printer must be running a compatible server (OctoPrint or Moonraker) that accepts G-code commands via HTTP POST. For Kasa devices, the button communicates directly via the LAN.

> [!CAUTION]
> > This is a **software** E-Stop button. It does not cut physical power or ensure printer safety.
> >
> > **Use at your own risk.** This project provides **no hardware safety mechanisms**.
> >
> > We are **not responsible** for any damage or injury.

## 📚 Table of Contents
- [✨ Features](#-features)
- [🧰 Hardware](#-hardware)
- [🖥️ PlatformIO Usage](#️-platformio-usage)
  - [Example:](#example)
- [📦 Pin Configuration](#-pin-configuration)
- [⚙️ Configuration Fields](#️-configuration-fields)
- [📡 API Logic](#-api-logic)
- [📚 Requirements](#-requirements)
- [📜 License](#-license)

## ✨ Features

- WiFiManager captive portal for first-time setup
- Persistent configuration in EEPROM
- Configurable:
  - Base URL (or local IP for Kasa)
  - API Key (if needed)
  - G-code (or `on` / `off` for Kasa)
  - Server type: `octo`, `moon`, or `kasa`
- Debounced button input
- Long-press (3 seconds) to reset settings
- LED feedback for:
  - Boot
  - Button press
  - Configuration reset

## 🧰 Hardware

- ESP8266 board (e.g., NodeMCU or Wemos D1 Mini)
- Momentary pushbutton
- Optional: LED with 330Ω resistor

## 🖥️ PlatformIO Usage

```bash
make build           # Compile the firmware
make upload          # Upload to ESP8266 (use PORT=/dev/ttyUSB0 if needed)
make monitor         # Open serial monitor
make clean           # Clean build
````

### Example:

```bash
make upload PORT=/dev/ttyUSB0
make monitor
```

## 📦 Pin Configuration

| Function | Pin | Notes                 |
| -------- | --- | --------------------- |
| Button   | D1  | Pulled-up input       |
| LED      | D2  | Active LOW by default |

## ⚙️ Configuration Fields

When first powered on (or after reset), a captive portal will appear:

📶 **SSID**: `EstopConfigAP`

You will be prompted for:

| Field       | Example                   | Notes                                            |
| ----------- | ------------------------- | ------------------------------------------------ |
| Base URL    | `http://192.168.0.150`    | For Octo/Moon: server URL<br>For Kasa: device IP |
| API Key     | `abc123...`               | OctoPrint / Moonraker key<br>Not used for Kasa   |
| G-code      | `M112` or `on` / `off`    | G-code to send OR switch command for Kasa        |
| Server Type | `octo`, `moon`, or `kasa` | Determines how the command is sent               |

> \[!WARNING]
>
> > API keys are stored in EEPROM and sent in plaintext. Do **not expose** this device to the public internet.
> >
> > Use a restricted-scope API key, and secure your network appropriately.

## 📡 API Logic

| Server Type | Target                     | Protocol | Payload Format                                                  | Header / Method                      |
| ----------- | -------------------------- | -------- | --------------------------------------------------------------- | ------------------------------------ |
| `octo`      | `/api/printer/command`     | HTTP     | `{ "command": "M112" }`                                         | `X-Api-Key: <key>` (POST)            |
| `moon`      | `/printer/gcode/script`    | HTTP     | `{ "script": "M112" }`                                          | `Authorization: Bearer <key>` (POST) |
| `kasa`      | Local device IP, port 9999 | TCP      | JSON: `{"system":{"set_relay_state":{"state":1}}}` or `state:0` | Encrypted XOR payload via raw TCP    |

## 📚 Requirements

* [PlatformIO](https://platformio.org/)
* ESP8266 board platform
* Auto-installed libraries:

  * `WiFiManager`
  * `ESP8266HTTPClient`
  * `EEPROM`

## 📜 License

MIT Licensed – see [LICENSE](LICENSE) for details.

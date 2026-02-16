# 🔴 ESP-Stop Button for 3D Printers

This project implements a simple emergency stop (E-Stop) button using an ESP8266 and a physical button.

When pressed, it sends a command to a server, smart device, or 3D printer, depending on the selected mode. Supported integrations include:

- **OctoPrint** (3D printer)
- **Moonraker / Klipper** (3D printer)
- **Bambu Lab** (3D printer via MQTT)
- **TP-Link Kasa** (local LAN smart plugs/switches)

> [!NOTE]
> Your printer must be running a compatible server (OctoPrint or Moonraker) that accepts G-code commands via HTTP POST. For Bambu Lab printers, the button uses MQTT over TLS. For Kasa devices, the button communicates directly via the LAN.

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

- **Persistent Web Interface** - Access configuration anytime via browser
- **WiFiManager** captive portal for first-time setup
- **Smart Button Controls**:
  - Short press: Send emergency stop command
  - Hold 1.5s: Open configuration portal
  - Hold 3s: Factory reset
- **Improved TP-Link Kasa Support**:
  - Single outlet devices (HS100, HS103, KP115, etc.)
  - Multi-outlet power strips (HS300, KP303, etc.)
  - Special handling for KP200 dual-outlet quirks
- **Immediate Klipper E-Stop**:
  - Uses `/printer/emergency_stop` endpoint for instant shutdown
  - Falls back to `/printer/gcode/script` if endpoint unavailable (custom Klipper builds)
  - No waiting for command queue like standard M112
- **Persistent Configuration** in EEPROM
- Configurable:
  - Base URL (or local IP for Kasa/Bambu)
  - API Key / Access Code (if needed)
  - G-code / Command / Serial Number
  - Server type: `octo`, `moon`, `kasa`, or `bambu`
- Debounced button input with visual LED feedback
- LED status indicators for all operations

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

## ⚙️ Configuration

### Web Interface

After connecting to WiFi, access the configuration page at `http://<device-ip>/`

The web interface provides:
- Current configuration status
- WiFi signal strength
- Configuration editor
- Factory reset option

### WiFiManager Portal

When first powered on (or after reset), a captive portal will appear:

📶 **SSID**: `EstopConfigAP`

### Configuration Fields

| Field       | Example                      | Notes                                            |
| ----------- | ---------------------------- | ------------------------------------------------ |
| Base URL    | `http://192.168.0.150:7125`  | For Octo/Moon: full server URL<br>For Kasa/Bambu: just IP address |
| API Key     | `abc123...`                  | OctoPrint / Moonraker: API key<br>Bambu: Access Code (from printer settings)<br>Not used for Kasa |
| G-code      | `M112` or `on0` / `off1`     | Octo/Moon: G-code command<br>Kasa: switch command<br>Bambu: Printer Serial Number |
| Server Type | `octo`, `moon`, `kasa`, or `bambu` | Determines how the command is sent               |

### Accessing Configuration

You can reconfigure the device in three ways:
1. **Web Interface**: Navigate to `http://<device-ip>/config`
2. **Button Hold**: Hold button for 1.5 seconds to open config portal
3. **Factory Reset**: Hold button for 3 seconds to clear all settings

> \[!WARNING]
>
> > API keys are stored in EEPROM and sent in plaintext. Do **not expose** this device to the public internet.
> >
> > Use a restricted-scope API key, and secure your network appropriately.

## 📡 API Logic

| Server Type | Target                       | Protocol | Payload Format                                                    | Header / Method                      |
| ----------- | ---------------------------- | -------- | ----------------------------------------------------------------- | ------------------------------------ |
| `octo`      | `/api/printer/command`       | HTTP     | `{ "command": "M112" }`                                           | `X-Api-Key: <key>` (POST)            |
| `moon`      | `/printer/emergency_stop`    | HTTP     | Empty (for M112)<br>`{ "script": "<cmd>" }` (for other commands) | `Authorization: Bearer <key>` (POST) |
|             | `/printer/gcode/script`      |          | Fallback if emergency_stop returns 404                            |                                      |
| `bambu`     | `device/<serial>/request`    | MQTT/TLS | `{"print":{"command":"stop","param":""}}`                         | Username: `bblp`, Password: Access Code (Port 8883) |
| `kasa`      | Local device IP, port 9999   | TCP      | Auto-detects single vs multi-outlet format                        | Encrypted XOR payload via raw TCP    |

### Kasa Command Formats

The firmware automatically detects device type and uses the appropriate format:

**Single Outlet Devices** (HS100, HS103, KP115):
```json
{"system":{"set_relay_state":{"state":1}}}
```

**Multi-Outlet Devices** (HS300, KP303):
```json
{"context":{"child_ids":["<outlet_id>"]},"system":{"set_relay_state":{"state":1}}}
```

**Command Syntax**:
- `on` / `off` - Control outlet 0
- `on0` / `off0` - Control outlet 0 explicitly
- `on1` / `off1` - Control outlet 1
- `0` / `1` / `2` - Turn on outlet N

### Bambu Lab Configuration

For Bambu Lab printers, the device connects via MQTT over TLS (port 8883):

**Required Settings**:
- **Base URL**: Printer's IP address only (e.g., `192.168.1.100`)
- **API Key**: Access Code from printer settings (Settings → Network → Access Code)
- **G-code Field**: Printer Serial Number (found in Bambu Studio or printer network settings)

**Emergency Stop Command**:
```json
{"print":{"command":"stop","param":""}}
```

Published to MQTT topic: `device/<serial>/request`

**MQTT Connection**:
- Broker: The printer itself
- Port: 8883 (MQTT over TLS)
- Username: `bblp`
- Password: Access Code
- TLS: Required (certificate verification disabled for self-signed cert)

## 📚 Requirements

* [PlatformIO](https://platformio.org/)
* ESP8266 board platform
* Auto-installed libraries (via `lib_deps` in `platformio.ini`):

  * `WiFiManager`
  * `ESP8266HTTPClient`
  * `PubSubClient` (for Bambu MQTT support)
  * `EEPROM`

## 📜 License

MIT Licensed – see [LICENSE](LICENSE) for details.

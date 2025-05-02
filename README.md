# 🔴 ESP-Stop Button for 3D Printers

This project implements a simple emergency stop (E-Stop) button using an ESP8266 and a physical button.

When pressed, it sends a G-code command (default: `M112`) to a 3D printer server running either **OctoPrint** or **Moonraker API (Klipper)**.

> [!NOTE]
> Your printer must be running a compatible server (OctoPrint or Moonraker) that accepts G-code commands via HTTP POST requests. The `M112` command is a common E-Stop command for many 3D printers, but you can configure it to send any G-code command you prefer.

> [!CAUTION]
> > This is a **software** E-Stop button. It does not provide any hardware safety features such as fuses or relays to cut power.
> >
> > **Use at your own risk.** Always ensure proper safety measures are in place when operating 3D printers.
> >
> > **This project is not responsible for any damage or injury caused by its use.**

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

- WiFiManager captive portal for initial configuration
- Configure:
  - Server base URL (e.g. `http://192.168.0.150`)
  - API key
  - G-code command to send (e.g. `M112`)
  - Server type: `octo` or `moon`
- EEPROM persistence
- Debounced button input
- Long-press (3 seconds) during boot resets configuration
- LED feedback for:
  - Boot
  - Button press
  - Reset hold

## 🧰 Hardware

- ESP8266 board (e.g., NodeMCU or Wemos D1 Mini)
- Pushbutton (momentary, normally open)
- Optional LED with 330Ω resistor (if board LED is not suitable)

## 🖥️ PlatformIO Usage

```bash
make build           # Compile the firmware
make upload          # Upload to ESP8266 (use PORT=/dev/ttyUSB0 if needed)
make monitor         # Open serial monitor
make clean           # Clean build
```

### Example:

```bash
make upload PORT=/dev/ttyUSB0
make monitor
```

## 📦 Pin Configuration

| Function     | Pin  | Notes                 |
|--------------|------|-----------------------|
| Button       | D1   | Pulled-up input       |
| LED          | D2   | Active LOW by default |

## ⚙️ Configuration Fields

On first boot or after a reset (hold button at boot), a captive portal will appear at:

📶 **SSID**: `EstopConfigAP`

You’ll be prompted to enter:

| Field         | Example                     | Notes                                     |
|---------------|-----------------------------|-------------------------------------------|
| Base URL      | `http://192.168.0.150`      | Server IP or hostname                     |
| API Key       | `abc123...`                 | OctoPrint or Moonraker key                |
| G-code        | `M112`                      | Command sent when button is pressed       |
| Server Type   | `octo` or `moon`            | Determines which API to use               |

> [!WARNING]
> 
> > Your API key is stored in EEPROM and sent in headers, likely unencrypted in a default OctoPrint/Moonraker setup.
> >
> > Ensure your network is secure and the server is not exposed to the internet, use a **limited-scope key** if possible.
> >
> > This project is not responsible for any data leaks or unauthorized access to your printer.

## 📡 API Logic

| Server Type | Endpoint                  | Method | Payload Format           | Header                        |
|-------------|---------------------------|--------|---------------------------|-------------------------------|
| `octo`      | `/api/printer/command`    | POST   | `{ "command": "M112" }`   | `X-Api-Key: <key>`            |
| `moon`      | `/printer/gcode/script`   | POST   | `{ "script": "M112" }`    | `Authorization: Bearer <key>`|

## 📚 Requirements

- [PlatformIO](https://platformio.org/)
- ESP8266 board support
- Libraries (auto-installed):
  - `WiFiManager`
  - `ESP8266HTTPClient`
  - `EEPROM`

## 📜 License

MIT Licensed – see [LICENSE](LICENSE) for details.

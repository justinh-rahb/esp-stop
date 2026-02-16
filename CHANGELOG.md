# Changelog

All notable changes to ESP-Stop will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added
- **Bambu Lab printer support** via MQTT over TLS
  - Direct connection to Bambu Lab printers using MQTT (port 8883)
  - Emergency stop command sends `stop` command to printer
  - Secure TLS connection with access code authentication
  - Configuration requires: Printer IP, Access Code, and Serial Number
- **Dynamic web configuration UI**
  - Field labels, placeholders, and hints automatically update based on selected printer type
  - Context-aware help text for each integration (OctoPrint, Moonraker, Kasa, Bambu)
  - Improved user experience with JavaScript-driven UI updates
  - Makes configuration more intuitive and reduces user errors
- Added `PubSubClient` library dependency for MQTT support

### Changed
- Configuration form now uses dynamic JavaScript to update UI elements
- Improved documentation with detailed Bambu Lab setup instructions
- Enhanced feature list in README to highlight dynamic UI and Bambu support

### Fixed
- UTF-8 encoding issue with arrow characters (→) in web interface
  - Changed to HTML entities (`&rarr;`) for proper display

## [Previous Releases]

### Prior Features
- OctoPrint support with M112 emergency stop
- Moonraker/Klipper support with immediate emergency_stop endpoint
- TP-Link Kasa smart plug/switch support (single and multi-outlet)
- WiFiManager captive portal for easy WiFi setup
- EEPROM persistent configuration storage
- Smart button controls (short press, 1.5s hold, 3s factory reset)
- LED status indicators
- Debounced button input
- Web-based configuration interface

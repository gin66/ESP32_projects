# ESP32 Projects

## Structure

All ESP32 projects are now organized under `ESP32/` with the former template-based structure.

### Projects

| # | Project | Description |
|---|---------|-------------|
| 00 | starter | Template starter project |
| 01 | qrcode | QR code generation |
| 02 | garage | Garage door controller |
| 03 | monitor | Monitoring system |
| 04 | herd | Heating system |
| 05 | heizung | Heating controller |
| 06 | wasserzaehler | Water meter (ESP32-CAM) |
| 07 | testbot | Test bot |
| 08 | eink | E-ink display |
| 09 | psramcheck | PSRAM testing |
| 10 | mpu6050 | MPU6050 sensor |
| 11 | esp32cam_servo | ESP32-CAM servo control |
| 12 | esp32cam_stepper | ESP32-CAM stepper control |
| 13 | s0_reader | S0 energy meter reader |
| 14 | power_control | Power control |
| 15 | power_log_eink | Power logging (E-ink) |
| 16 | main_control | Main control system |
| 17 | heizungssteuerung | Heating control |
| 18 | esp32_cam_xy6020l | ESP32-CAM XY6020L |
| 19 | CYD-project | CYD display with LVGL UI |
| 20 | power_supply_controller | Power supply controller |
| 21 | matter | Matter device example |
| 22 | WS2814_Lichtband | WS2814 LED strip |
| 23 | LED_matrix | LED matrix |
| 24 | demo_tdisplay | TTGO T-Display demo |
| 25 | LED_matrix_C3 | LED matrix (ESP32-C3) |
| 26 | CYD_CAN_monitor | CAN bus monitor (CYD) |
| 27 | cyd_starter | CYD starter project |

### Template System

All projects share a common template (`prj_template/`) via symlinks rather than duplicating code. Template modules include:

- `tpl_system` — FreeRTOS task management, deep sleep, boot count, RTC data
- `tpl_wifi` — WiFi connection + OTA firmware updates
- `tpl_webserver` — HTTP server
- `tpl_websocket` — WebSocket server/client
- `tpl_net_watchdog` — Network health monitoring with reboot
- `tpl_command` — Command dispatch framework
- `tpl_telegram` — Telegram bot integration
- `tpl_display` — CYD board abstraction (2.8″ and 3.5″)

### Board Definitions

Board-specific configurations are in `boards/`:

- ESP32-38Pin-DevKit-Generic
- ESP32-C3-SuperMini
- ESP32-C6-DevKitC-1
- ESP32-CAM
- LILYGO-TTGO-T5-V2.3 (E-ink)
- SUNTON CYD 2.4″ / 2.8″ / 3.5″
- T-Display 1.14″
- TENSTAR ESP32-C3/S3 Zero

### Getting Started

1. Symlink a project: `make links`
2. Build: `pio run -e esp32ota -p <project_dir>`
3. Upload: `pio run -e esp32ota --target upload`

See `AGENTS.md` for full build system documentation.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

All projects use **PlatformIO** with the Arduino framework for ESP32.

```bash
# Build a specific project (OTA upload, default)
cd XX_project && pio run

# Build and upload via OTA (default env)
cd XX_project && pio run --target upload

# Build and upload via USB
cd XX_project && pio run -e esp32usb --target upload

# Monitor serial output
cd XX_project && pio device monitor

# Upload filesystem (SPIFFS)
cd XX_project && pio run --target uploadfs
```

## Code Formatting

```bash
make fmt   # runs clang-format (Google style) on all .h/.c/.cpp, prettier on HTML
```

## Template Link Management

All projects symlink to shared template files. To regenerate all symlinks:

```bash
make links
```

This creates symlinks in every `XX_project/{include,src,data,tools}/` pointing to `prj_template/`.

## Repository Architecture

### Template System (`prj_template/`)

The core of the codebase. Every project (`00_starter` through `20_power_supply_controller`) symlinks to this directory rather than duplicating code.

**Template modules** (each has a `.h` in `include/` and `.cpp` in `src/`):
- `tpl_system` — FreeRTOS task management, deep sleep, boot count, RTC data, `WATCH()` macro for watchpoints
- `tpl_wifi` — WiFi connection + OTA firmware updates
- `tpl_webserver` — HTTP server
- `tpl_websocket` — WebSocket server/client
- `tpl_net_watchdog` — Pings a gateway (`NET_WATCHDOG` define) and reboots on failure
- `tpl_command` — Command dispatch framework
- `tpl_telegram` — Telegram bot integration
- `tpl_esp_camera` / `tpl_app_camera` — ESP32-CAM camera capture
- `tpl_broadcast` — UDP broadcast messaging

**Global state structures** (defined in `tpl_system.h`):
- `tpl_tasks` — FreeRTOS task handles for all template tasks
- `tpl_config` — Runtime configuration flags (deep sleep allowed, OTA ongoing, WiFi state, Telegram state, stack info, etc.)
- `tpl_spiffs_config` / `TplSpiffsConfig` — SPIFFS-persisted config (version-checked, saved to `/config.ini`)
- `rtc_data` — RTC memory (survives deep sleep): boot count, watchpoint

### Secrets (`../.private/`)

Located outside the repo at `.private/` (one level up). Symlinked into each project's `src/`:
- `private_wifi_secrets.cpp` → `wifi_secrets.cpp`
- `private_bot.h` — Telegram bot tokens (`BOTtoken`, etc.)
- `private_sha.h` — Authentication secrets

### Typical `main.cpp` Pattern

```cpp
#include "template.h"

void setup() {
  tpl_system_setup(0);  // arg = deep sleep seconds, 0 = disabled
  Serial.begin(115200);
  tpl_wifi_setup(true, true, (gpio_num_t)tpl_ledPin);
  tpl_webserver_setup();
  tpl_websocket_setup(NULL, NULL);
  tpl_net_watchdog_setup();
  tpl_command_setup(NULL);
  // project-specific setup here
}

void loop() {
  vTaskDelay(10 / portTICK_PERIOD_MS);
}
```

### `platformio.ini` Structure

All projects define a `[common]` section with shared settings and two environments:
- `esp32ota` — OTA upload via mDNS (`${common.hostname}.local:3232`), default
- `esp32usb` — USB serial upload (port hardcoded per project)

Key build flags used across projects:
- `-DHOSTNAME=\"esp32_XX\"` — Device hostname
- `-DNET_WATCHDOG=\"192.168.1.1\"` — IP to ping for network health
- `-DCORE_DEBUG_LEVEL=5` (or `1`) — ESP-IDF log verbosity
- `-DCONFIG_SPIRAM_CACHE_WORKAROUND` — Required for PSRAM stability
- Board-type flags: `IS_ESP32CAM`, `IS_ESP32CYD`, `HAS_STEPPERS`

### Project-Specific Notes

- **15_power_log_eink**: E-ink display (GxEPD), SML smart meter protocol (libsml), CAN bus. Board: `LILYGO_V2_3`.
- **19_CYD-project**: LVGL 9.x UI with EEZ-Studio generated code, TFT_eSPI (ILI9341), XPT2046 touchscreen. All TFT pins defined as build flags. Backlight via LEDC PWM (pin 21), not TFT_eSPI.
- **06_wasserzaehler**: ESP32-CAM water meter image recognition.
- **20_power_supply_controller**: Newest project, still being set up.

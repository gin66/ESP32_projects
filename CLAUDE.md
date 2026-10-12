# AGENTS.md

This file provides guidance to AI coding agents when working with code in this repository.

## Build System

All projects use **PlatformIO** with the Arduino framework for ESP32. Every project has two environments: `esp32ota` (OTA upload, default) and `esp32usb` (USB serial upload). You **must always specify the environment** with `-e`.

```bash
# Build only (OTA env)
cd XX_project && pio run -e esp32ota

# Build only (USB env)
cd XX_project && pio run -e esp32usb

# Build and upload via OTA
cd XX_project && pio run -e esp32ota --target upload

# Build and upload via USB
cd XX_project && pio run -e esp32usb --target upload

# Monitor serial output
cd XX_project && pio device monitor

# Upload filesystem (SPIFFS) via OTA
cd XX_project && pio run -e esp32ota --target uploadfs

# Upload filesystem (SPIFFS) via USB
cd XX_project && pio run -e esp32usb --target uploadfs
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

All projects define a `[common]` section with shared settings and two environments. There is no project-specific config file — all configuration options are set using `platformio.ini` build flags.

- `esp32ota` — OTA upload via mDNS (`${common.hostname}.local:3232`), default
- `esp32usb` — USB serial upload (port hardcoded per project)

Key build flags used across projects:

- `-DHOSTNAME=\"esp32_XX\"` — Device hostname
- `-DNET_WATCHDOG=\"192.168.1.1\"` — IP to ping for network health
- `-DCORE_DEBUG_LEVEL=5` (or `1`) — ESP-IDF log verbosity
- `-DCONFIG_SPIRAM_CACHE_WORKAROUND` — Required for PSRAM stability
- Board-type flags: `IS_ESP32CAM`, `IS_ESP32_CYD_28`, `IS_ESP32_CYD_35`, `HAS_STEPPERS`
- `-DTFT_ROTATION=n` — Display rotation (0–3), project-specific

### Project-Specific Notes

- **15_power_log_eink**: E-ink display (GxEPD), SML smart meter protocol (libsml), CAN bus. Board: `LILYGO_V2_3`. Uses `tpl_broadcast`.
- **19_CYD-project**: 2.8″ CYD (ILI9341) with LVGL 9.x UI, EEZ-Studio generated code, power monitoring. Uses `tpl_display` abstraction, `tpl_sd` module, `tpl_broadcast`.
- **26_CYD_CAN_monitor**: Multi-board CAN bus monitor with LVGL UI. Supports both 2.8″ and 3.5″ CYD boards via `tpl_display` abstraction. Uses CAN bus (TWAI driver), protocol analyzer.
- **06_wasserzaehler**: ESP32-CAM water meter image recognition. Uses `tpl_telegram`.
- **20_power_supply_controller**: Newest project, still being set up.
- **21_matter**: Example Matter device.
- **22/23**: Controllable LEDs.

### CYD Board Abstraction (`tpl_display`)

The template provides a board abstraction layer for Cheap Yellow Displays (CYD)
in `prj_template/include/tpl_display.h` and `prj_template/src/tpl_display.cpp`.
It eliminates all hardcoded display/touch configuration from individual project
`main.cpp` files.

**Supported boards:**

| Board | Define | Driver | Resolution | Touch SPI |
|-------|--------|--------|------------|-----------|
| 2.8″ (SUNTON 2432S028R) | `IS_ESP32_CYD_28` | ILI9341 | 240×320 | Private VSPI (pins 25/39/32) |
| 3.5″ (SUNTON 3248S035R) | `IS_ESP32_CYD_35` | ST7796 | 320×480 | Shared display VSPI (pins 12/13/14) |

**Usage:**

1. Define `IS_ESP32_CYD_28` or `IS_ESP32_CYD_35` in `platformio.ini` build_flags.
2. Include `template.h` (which includes `tpl_display.h`).
3. Declare global: `TFT_eSPI tft = TFT_eSPI();`
4. Declare a draw buffer: `uint32_t draw_buf[CYD_DRAW_BUF_SIZE / 4];`
5. In `setup()`:
   ```cpp
   lv_display_t *disp = tpl_cyd_display_init(TFT_ROTATION, draw_buf,
                                             sizeof(draw_buf));
   lv_display_set_default(disp);  // Required in LVGL 9.x
   tpl_cyd_touch_init(touchscreen, TFT_ROTATION);
   tpl_cyd_backlight_init(7, BACKLIGHT_DIM);
   tpl_cyd_rgb_init(true);  // turn off floating RGB pins
   ```
   **Important:** `lv_display_set_default(disp)` is required in LVGL 9.x.
   Without it, `lv_indev_create()` and any EEZ Studio UI code (`ui_init()`) will
   fail with "no display was created yet" errors because they look up the
   default display internally.
   The abstraction uses the global `tft` object (pins from
   `user_setup_declared.h`) with a custom LVGL flush callback — it does NOT
   use `lv_tft_espi_create()` (which creates its own unconfigured TFT_eSPI
   and crashes).
5. Touch callback (single function call):
   ```cpp
   static void touchscreen_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
       bool pressed;
       tpl_cyd_touch_read(touchscreen, data, &pressed,
                          CYD_WIDTH, CYD_HEIGHT, TFT_ROTATION);
       data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
   }
   ```

**Key functions:**

| Function | Purpose |
|----------|---------|
| `tpl_cyd_display_init(rotation, buf, buf_size)` | Creates LVGL display with own TFT_eSPI instance; returns `lv_display_t*` (NULL if buf is NULL) |
| `tpl_cyd_touch_init(touchscreen, rotation)` | Initializes touch SPI (private or shared), sets rotation |
| `tpl_cyd_touch_read(...)` | **Core**: full rotation pipeline (ADC → calibration → logical rotation) |
| `tpl_cyd_backlight_init(channel, brightness)` | Sets up LEDC PWM for backlight |
| `tpl_cyd_rgb_init(turn_off)` | Configures RGB LED pins (HIGH = off works for both boards) |
| `tpl_cyd_light_sensor_read()` | Reads CDS photoresistor on 3.5″ boards (returns 0 on 2.8″) |

**Adding a new CYD variant** requires editing only `tpl_display.h` (add a new
`#elif defined(IS_ESP32_CYD_XX)` block with dimensions, driver, pins, and
calibration ranges) plus adding one `-D` flag to `platformio.ini`. No
`main.cpp` changes needed.

Full documentation: `prj_template/docs/cyd_board_abstraction.md`.

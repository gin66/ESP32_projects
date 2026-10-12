# 27_cyd_starter — CYD Board Abstraction Demo

A minimal project demonstrating the `tpl_display` CYD board abstraction layer.

## Quick Start

1. **Select your board** in `platformio.ini`:
   - 2.8″ CYD (SUNTON 2432S028R): `-D IS_ESP32_CYD_28` (default)
   - 3.5″ CYD (SUNTON 3248S035R): `-D IS_ESP32_CYD_35`

2. **Build and upload**:
   ```bash
   # OTA (default environment)
   pio run -e esp32ota

   # USB serial
   pio run -e esp32usb
   ```

## What This Demo Shows

- Board detection (2.8″ vs 3.5″)
- Display initialization (TFT_eSPI + LVGL)
- Touch initialization (private or shared SPI bus)
- Full rotation pipeline (ADC → calibration → logical rotation)
- Backlight PWM control
- RGB LED pin management
- Simple LVGL UI with frame counter

## Abstraction Functions Used

| Function | Purpose |
|----------|---------|
| `tpl_cyd_display_init(rotation)` | Init TFT_eSPI + LVGL driver |
| `tpl_cyd_touch_init(touchscreen, rotation)` | Init touch SPI (private/shared) |
| `tpl_cyd_touch_read(...)` | Full rotation pipeline (core) |
| `tpl_cyd_backlight_init(channel, brightness)` | LEDC PWM backlight |
| `tpl_cyd_rgb_init(turn_off)` | RGB LED pin management |

## Adding a New CYD Board

Edit `prj_template/include/tpl_display.h`:
1. Add a new `#elif defined(IS_ESP32_CYD_XX)` block
2. Define dimensions, driver, pins, calibration ranges
3. Add `-D IS_ESP32_CYD_XX` to `platformio.ini`

No `main.cpp` changes needed.

## Documentation

Full white paper: `prj_template/docs/cyd_board_abstraction.md`

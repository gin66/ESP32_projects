# CYD Board Abstraction — Implementation Todo List

## Phase 1: Create `tpl_display.h` / `tpl_display.cpp`

- [ ] **1.1** Define board constants in `tpl_display.h` (dimensions, driver, all pins, calibration defaults)
  - 2.8″ CYD block: `IS_ESP32_CYD_28`
  - 3.5″ CYD block: `IS_ESP32_CYD_35`
  - `#error` if neither defined
  - `CYD_SCREEN_WIDTH`, `CYD_SCREEN_HEIGHT`, `CYD_DRIVER`
  - `CYD_WIDTH`, `CYD_HEIGHT` (portrait aliases)
  - All pin constants: `CYD_TFT_*`, `CYD_TOUCH_*`, `CYD_BACKLIGHT_PIN`, `CYD_RGB_*`, `CYD_BOOT_PIN`, `CYD_SPEAKER_PIN`, `CYD_CDS_PIN`
  - Touch calibration defaults (final, no overrides)

- [ ] **1.2** Implement non-font TFT_eSPI defines in `tpl_display.h`
  - `USER_SETUP_LOADED`, `TFT_INVERSION_ON`, `TFT_BACKLIGHT_ON`, `TFT_RGB_ORDER` (3.5″ only)
  - `SPI_FREQUENCY`, `SPI_READ_FREQUENCY`, `SPI_TOUCH_FREQUENCY`
  - These go in `tpl_display.h`; font defines stay in `platformio.ini`

- [ ] **1.3** Implement `tpl_cyd_display_init(uint8_t rotation)`
  - Configures global `TFT_eSPI tft` with board-specific settings
  - Calls `tft.setRotation(rotation)`
  - Returns `lv_display_t*` from `lv_tft_espi_create()`
  - Does NOT create/own the `TFT_eSPI` instance (stays in project code)

- [ ] **1.4** Implement `tpl_cyd_touch_init(XPT2046_Touchscreen &touchscreen, uint8_t rotation)`
  - **Enforce display-first ordering**: checks flag set by `tpl_cyd_display_init()`
  - On 3.5″: if display not yet initialized → `assert()` + serial message
  - On 2.8″ (private SPI): order irrelevant
  - 2.8″: `touchscreenSPI.begin()` with private pins
  - 3.5″: `touchscreen.begin(TFT_eSPI::getSPIinstance())` (shared bus)
  - Calls `touchscreen.setRotation(rotation)`

- [ ] **1.5** Implement `tpl_cyd_touch_read(...)` — **core function**
  - Full rotation pipeline: ADC → XPT2046 rotation → calibration → logical rotation
  - Takes `rotation` parameter, returns coordinates in requested orientation
  - Ready for LVGL directly — no rotation switch statements in project code

- [ ] **1.6** Implement `tpl_cyd_backlight_init(int pwm_channel, uint8_t initial_brightness)`

- [ ] **1.7** Implement `tpl_cyd_rgb_init(bool turn_off_rgb)`
  - Both boards: sets pins 4/16/17 HIGH
  - 2.8″: inverted logic (HIGH = LEDs off)
  - 3.5″: floating pins (HIGH = prevent floating)
  - Same state, different reasons — works for both

- [ ] **1.8** Implement `tpl_cyd_light_sensor_read()`
  - Returns `analogRead(CYD_CDS_PIN)` on 3.5″ boards
  - Returns 0 on 2.8″ boards (no CDS component)
  - Optional helper — projects that don't need it just don't call it

## Phase 2: Update `template.h`

- [ ] **2.1** Add `#include "tpl_display.h"` to `template.h`

## Phase 3: Update Project 26 (3.5″ environment)

- [ ] **3.1** Replace `#if defined(ESP32_35_BOARD)` with `#if defined(IS_ESP32_CYD_35)` everywhere in `main.cpp`
- [ ] **3.2** Remove all per-environment TFT/touch defines from `platformio.ini` (non-font TFT_eSPI defines move to `tpl_display.h`)
- [ ] **3.3** Replace `main.cpp` display/touch code with `tpl_display` calls (~80 lines → ~10 lines)
- [ ] **3.4** Test on 3.5″ hardware

## Phase 4: Update Project 26 (2.8″ environments)

- [ ] **4.1** Add `IS_ESP32_CYD_28` to `esp32ota` and `esp32usb` envs in `platformio.ini`
- [ ] **4.2** Remove all per-environment TFT/touch defines from `platformio.ini`
- [ ] **4.3** Replace `main.cpp` display/touch code with `tpl_display` calls
- [ ] **4.4** Test on 2.8″ hardware

## Phase 5: Update Project 19

- [ ] **5.1** Replace `-D IS_ESP32CYD` with `-D IS_ESP32_CYD_28` in all environments
- [ ] **5.2** Remove all per-environment TFT/touch defines from `platformio.ini`
- [ ] **5.3** Replace `main.cpp` display/touch code with `tpl_display` calls
- [ ] **5.4** Remove `include/tft_setup.h` (duplicated TFT_eSPI User_Setup.h)
- [ ] **5.5** Test on 2.8″ hardware

## Phase 6: Documentation

- [ ] **6.1** Update `AGENTS.md` with CYD board abstraction section
- [ ] **6.2** Add `tpl_display.h` documentation to `prj_template/README`
- [ ] **6.3** Add CYD starter project (`27_cyd_starter`) demonstrating the abstraction
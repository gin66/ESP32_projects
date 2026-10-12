# White Paper: CYD Board Abstraction Layer

## Generalizing 2.8″ and 3.5″ Cheap Yellow Displays in prj_template

**Date:** 2026-10-07
**Author:** ESP32_projects AGENTS
**Status:** Proposal (v3 — updated after review)

---

## 1. Problem Statement

Two CYD-based projects in this repository (`19_CYD-project` and `26_CYD_CAN_monitor`) each contain **hardcoded, duplicated** display configuration:

| Item | Project 19 (2.8″) | Project 26 (2.8″ + 3.5″) |
|------|--------------------|--------------------------|
| Screen dimensions | Hardcoded `#define SCREEN_WIDTH 320` / `SCREEN_HEIGHT 240` in `main.cpp` | `#ifndef` defaults in `main.cpp` + per-environment flags in `platformio.ini` |
| Touch calibration | Hardcoded `map(p.x, 200, 3700, 1, 320)` / `map(p.y, 240, 3800, 1, 240)` in `main.cpp` | Per-environment `TOUCH_X_MIN`, `TOUCH_X_MAX`, `TOUCH_Y_MIN`, `TOUCH_Y_MAX` in `platformio.ini` |
| Rotation | Hardcoded `tft.setRotation(3)` / `touchscreen.setRotation(3)` in `main.cpp` | `#ifndef TFT_ROTATION` default + per-environment flag in `platformio.ini` |
| Touch mapping | Manual rotation switch in `touchscreen_read()` callback | Manual rotation switch in `touchscreen_read_cb()` — but also has LVGL rotation transform |
| Display driver | `ILI9341_2_DRIVER` (2.8″ Sunton 2432S028R) | 2.8″: `ILI9341_2_DRIVER`; 3.5″: `ST7796_DRIVER` (3248S035R) |
| Touch SPI wiring | Dedicated SPI instance (`touchscreenSPI`) with pins 32/39/25 | 2.8″: same; 3.5″: shares display SPI (pins 12/13/14) |
| Backlight pin | `tpl_backlightPin` (21) via template | 2.8″: `tpl_backlightPin`; 3.5″: `TFT_BL` (27) |
| RGB LED handling | `tpl_ledPin_R/G/B` (4/16/17) via template | 3.5″: explicitly set pins 4/16/17 HIGH in `setup()` |

**Root cause:** Screen dimensions, touch calibration ranges, driver selection, and rotation handling are scattered across `main.cpp`, `platformio.ini`, and sometimes `User_Setup.h` — making it impossible to add a third CYD variant without editing every project.

**Secondary problem:** There is no abstraction for display/touch at all — every project duplicates ~60–80 lines of display/touch setup code in `main.cpp`.

---

## 2. Hardware Overview

### 2.8″ CYD (SUNTON 2432S028R)

| Parameter | Value |
|-----------|-------|
| Driver | ILI9341 (via ILI9341_2_DRIVER) |
| Native resolution | 240 × 320 (portrait) |
| Touch controller | XPT2046 |
| Touch CS | GPIO 33 |
| Touch IRQ | GPIO 36 |
| Touch SPI pins | MOSI=32, MISO=39, CLK=25 (dedicated SPI bus) |
| TFT SPI pins | MOSI=13, MISO=16, SCLK=14, CS=15 |
| TFT DC | GPIO 2 |
| TFT RST | GPIO 12 |
| Backlight | GPIO 21 (LEDC PWM) |
| RGB LED | Red=4, Green=16, Blue=17 (inverted logic) |
| Speaker | GPIO 26 (DAC2 / I2S) — only on 3.5″ board |
| CDS (light sensor) | GPIO 34 (analog input) — only on 3.5″ board |
| SD card | via `tpl_sd` module (HSPI) |

### 3.5″ CYD (SUNTON 3248S035R)

| Parameter | Value |
|-----------|-------|
| Driver | ST7796 |
| Native resolution | 320 × 480 (portrait) |
| Touch controller | XPT2046 |
| Touch CS | GPIO 33 |
| Touch SPI | **Shares** display SPI (SCLK=14, MISO=12, MOSI=13) |
| TFT SPI pins | MOSI=13, MISO=12, SCLK=14, CS=15 |
| TFT DC | GPIO 2 |
| TFT RST | **No reset** (tied to VCC internally) |
| Backlight | GPIO 27 (LEDC PWM) |
| RGB LED | GPIOs 4/16/17 (floating, must be pulled HIGH) |
| Speaker | GPIO 26 (DAC2 / I2S) — only on 3.5″ board |
| CDS (light sensor) | GPIO 34 (analog input) — only on 3.5″ board |
| SD card | via `tpl_sd` module (HSPI) |

### Usable GPIOs (not used by display/touch/backlight)

| GPIO | 2.8″ CYD | 3.5″ CYD | Notes |
|------|----------|----------|-------|
| 0 | ✅ | ✅ | **BOOT button** (strapping pin — must be HIGH at boot) |
| 1 | ✅ | ✅ | UART0 (not usable as GPIO during normal operation) |
| 3 | ✅ | ✅ | |
| 5 | ✅ | ✅ | |
| 18 | ✅ | ✅ | |
| 19 | ✅ | ✅ | |
| 20 | ✅ | ✅ | |
| 23 | ✅ | ✅ | |
| 25 | ✅ | — | Used as touch CLK on 2.8″ only |
| 26 | ✅ | ✅ | **Speaker** (DAC2 / I2S) — only on 3.5″ board |
| 34 | ✅ | ✅ | **CDS (light sensor)** — only on 3.5″ board (analog input) |
| 35 | ✅ | ✅ | Input-only |
| 36 | ✅ | ✅ | Touch IRQ (used by both) |
| 39 | ✅ | — | Touch MISO on 2.8″ only |

---

## 3. Architecture Analysis

### 3.1 TFT_eSPI Configuration Flow

TFT_eSPI uses **two** configuration mechanisms:

1. **Preprocessor defines in `User_Setup.h`** (library default): This file is a static template with all pins commented out. PlatformIO users override by passing `-D` flags in `platformio.ini`.

2. **Runtime `setRotation()`**: Called via `TFT_eSPI::setRotation(n)` where `n` is 0–3.

The library's `User_Setup_Select.h` includes `User_Setup.h` by default, and `TFT_eSPI.h` includes `User_Setup_Select.h`. PlatformIO `-D` flags override the preprocessor defines in `User_Setup.h`.

**Key insight:** All TFT_eSPI pin defines (`TFT_MOSI`, `TFT_MISO`, `TFT_SCLK`, `TFT_CS`, `TFT_DC`, `TFT_RST`, `TOUCH_CS`, `TFT_BL`) can be set via `-D` flags in `platformio.ini`. They do **not** need a local `User_Setup.h` file.

### 3.2 XPT2046 Library Rotation

The XPT2046 library (`XPT2046_Touchscreen.h`) has an internal `setRotation()` method that transforms raw ADC readings internally:

```cpp
void setRotation(uint8_t n) { rotation = n % 4; }
```

Inside `update()`, the raw ADC values (x, y from the touch controller) are transformed by rotation:

```
case 0:  xraw = 4095 - y;  yraw = x;       break;
case 1:  xraw = x;         yraw = y;       break;
case 2:  xraw = y;         yraw = 4095 - x; break;
case 3:  xraw = 4095 - x;  yraw = 4095 - y; break;
```

This gives **raw rotated ADC coordinates** (range ~0–4095). The application must then map these to screen coordinates using the touch calibration ranges.

### 3.3 LVGL 9 Rotation

LVGL 9's display driver (`lv_tft_espi_create`) accepts **native (portrait) resolution** and applies rotation internally:

```cpp
lv_display_t * lv_tft_espi_create(uint32_t hor_res, uint32_t ver_res, ...)
```

When `lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90)` is called, the driver maps to `tft->setRotation(1)` (landscape).

LVGL's `lv_display_rotate_point()` transforms the raw point from the input device callback to the display's **logical** (rotated) coordinates:

```
case 90:  px = ver_res - y - 1;  py = x;           break;
case 180: px = hor_res - x - 1; py = ver_res - y - 1; break;
case 270: px = y;              py = hor_res - x - 1; break;
```

### 3.4 The Rotation Pipeline

The abstraction handles the **full rotation pipeline** internally — this is the main value of `tpl_display`:

```
Touch Controller (ADC raw)
  → XPT2046 library (rotation transform → rotated ADC coords)
  → map() with calibration ranges (→ portrait screen coords)
  → apply logical rotation (→ screen coords in requested orientation)
  → return coordinates ready for LVGL hit testing
```

The abstraction's `tpl_cyd_touch_read()` takes the display rotation as a parameter and returns coordinates in the **logical (rotated) orientation** — ready for LVGL directly. No rotation switch statements. No landscape/portrait mapping. No double-rotation.

The project touch callback becomes a single function call:

```cpp
static void touchscreen_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    bool pressed;
    tpl_cyd_touch_read(touchscreen, data, &pressed,
                       CYD_WIDTH, CYD_HEIGHT, TFT_ROTATION);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
```

---

## 4. Proposed Solution

### 4.1 Define-Based Board Detection

Two preprocessor defines in `platformio.ini` build flags:

```
-D IS_ESP32_CYD_28    // 2.8" Sunton 2432S028R (ILI9341)
-D IS_ESP32_CYD_35    // 3.5" Sunton 3248S035R (ST7796)
```

Only one should be defined per build. If neither is defined, the build fails with a clear error.

**`IS_ESP32CYD` has been obsoleted.** The template's `tpl_system.h` already provides correct board-specific pin defines via `IS_ESP32_CYD_28` / `IS_ESP32_CYD_35`. No changes needed in the template — only project `platformio.ini` files need updating.

### 4.2 prj_template Module: `tpl_display.h` / `tpl_display.cpp`

A new template module that provides:

#### 4.2.1 Screen Dimensions

```cpp
// In tpl_display.h
#if defined(IS_ESP32_CYD_28)
  #define CYD_SCREEN_WIDTH  240
  #define CYD_SCREEN_HEIGHT 320
  #define CYD_DRIVER        ILI9341_2_DRIVER
#elif defined(IS_ESP32_CYD_35)
  #define CYD_SCREEN_WIDTH  320
  #define CYD_SCREEN_HEIGHT 480
  #define CYD_DRIVER        ST7796_DRIVER
#else
  #error "Define IS_ESP32_CYD_28 or IS_ESP32_CYD_35"
#endif

// Logical dimensions — orientation is the rotation parameter to tpl_cyd_touch_read()
#define CYD_WIDTH  CYD_SCREEN_WIDTH
#define CYD_HEIGHT CYD_SCREEN_HEIGHT
```

#### 4.2.2 Touch Calibration Ranges

Board-specific defaults — **final**, no project-level overrides. If a board has different calibration, edit `tpl_display.h` (which is exactly the abstraction's purpose).

```cpp
// Per-board calibration ranges (ADC values from XPT2046 library)
// These are the raw rotated ADC ranges after XPT2046 setRotation()

#if defined(IS_ESP32_CYD_28)
  #define CYD_TOUCH_X_MIN  200
  #define CYD_TOUCH_X_MAX  3700
  #define CYD_TOUCH_Y_MIN  240
  #define CYD_TOUCH_Y_MAX  3800
  // 2.8" board: dedicated touch SPI bus
  #define CYD_TOUCH_MOSI 32
  #define CYD_TOUCH_MISO 39
  #define CYD_TOUCH_CLK  25
  #define CYD_TOUCH_IRQ  36
  #define CYD_TOUCH_HAS_PRIVATE_SPI true
#elif defined(IS_ESP32_CYD_35)
  // 3.5" board: X/Y axes have different calibration due to panel wiring
  // Y axis is inverted relative to panel (top/bottom flipped), so ranges are inverted
  #define CYD_TOUCH_X_MIN  180
  #define CYD_TOUCH_X_MAX  3800
  #define CYD_TOUCH_Y_MIN  3900  // inverted: high value at top
  #define CYD_TOUCH_Y_MAX  220
  #define CYD_TOUCH_HAS_PRIVATE_SPI false  // shares display SPI
  // 3.5" board: no touch pin defines needed — uses display SPI
#endif
```

#### 4.2.3 All Usable Pin Declarations

The abstraction declares **all** board-specific pins in `tpl_display.h`:

```cpp
// === 2.8" CYD pins (IS_ESP32_CYD_28) ===
// Display SPI
#define CYD_TFT_MOSI   13
#define CYD_TFT_MISO   16
#define CYD_TFT_SCLK   14
#define CYD_TFT_CS     15
#define CYD_TFT_DC      2
#define CYD_TFT_RST    12

// Touch (dedicated SPI bus)
#define CYD_TOUCH_MOSI  32
#define CYD_TOUCH_MISO  39
#define CYD_TOUCH_CLK   25
#define CYD_TOUCH_CS    33
#define CYD_TOUCH_IRQ   36

// Backlight
#define CYD_BACKLIGHT_PIN 21

// RGB LED (inverted logic)
#define CYD_RGB_RED     4
#define CYD_RGB_GREEN  16
#define CYD_RGB_BLUE   17

// BOOT button (both boards)
#define CYD_BOOT_PIN 0  // strapping pin, must be HIGH at boot

// === 3.5" CYD pins (IS_ESP32_CYD_35) ===
// Display SPI
#define CYD_TFT_MOSI   13
#define CYD_TFT_MISO   12  // differs from 2.8"
#define CYD_TFT_SCLK   14
#define CYD_TFT_CS     15
#define CYD_TFT_DC      2
#define CYD_TFT_RST   -1   // no reset pin — tied to VCC internally

// Touch (shares display SPI)
#define CYD_TOUCH_CS    33
#define CYD_TOUCH_IRQ   36
// No touch MOSI/MISO/CLK — shares display SPI

// Backlight
#define CYD_BACKLIGHT_PIN 27  // differs from 2.8"

// Speaker (3.5″ only)
#define CYD_SPEAKER_PIN 26  // DAC2 / I2S

// CDS light sensor (3.5″ only)
#define CYD_CDS_PIN 34  // analog input

// RGB LED (floating, must be pulled HIGH)
#define CYD_RGB_RED     4
#define CYD_RGB_GREEN  16
#define CYD_RGB_BLUE   17

// BOOT button (both boards)
#define CYD_BOOT_PIN 0  // strapping pin, must be HIGH at boot
```

**Remaining usable GPIOs** (not used by display/touch/backlight):

| GPIO | 2.8″ CYD | 3.5″ CYD |
|------|----------|----------|
| 0 | ✅ | ✅ |
| 1 | ✅ | ✅ |
| 3 | ✅ | ✅ |
| 5 | ✅ | ✅ |
| 18 | ✅ | ✅ |
| 19 | ✅ | ✅ |
| 20 | ✅ | ✅ |
| 23 | ✅ | ✅ |
| 25 | ✅ | — (used as touch CLK) |
| 26 | ✅ | ✅ |
| 34 | ✅ | ✅ |
| 35 | ✅ | ✅ |
| 36 | ✅ | ✅ (touch IRQ) |
| 39 | ✅ | — (used as touch MISO) |

Projects can use any remaining GPIO freely. CAN bus pins are **project-specific** and accessible via a connector — the abstraction does not handle CAN.

#### 4.2.4 Backlight PWM

```cpp
/**
 * Initialize backlight PWM for CYD boards.
 * 
 * @param pwm_channel LEDC channel to use (7 recommended)
 * @param initial_brightness 0-255
 */
void tpl_cyd_backlight_init(int pwm_channel, uint8_t initial_brightness);
```

#### 4.2.5 Initialization Functions

```cpp
/**
 * Initialize TFT_eSPI and LVGL display driver for CYD boards.
 * 
 * The global TFT_eSPI tft object must be declared in project code
 * (as it is now). This function configures it with board-specific
 * settings and returns the LVGL display handle.
 * 
 * @param rotation Display rotation (0–3, mapped to LVGL rotation)
 * @return lv_display_t* pointer, or NULL on failure
 * 
 * The caller must still:
 *   - Declare global: TFT_eSPI tft = TFT_eSPI();
 *   - Create an input device with lv_indev_create()
 *   - Register a touch read callback via lv_indev_set_read_cb()
 */
lv_display_t * tpl_cyd_display_init(uint8_t rotation);

/**
 * Initialize the XPT2046 touchscreen.
 * 
 * Must be called AFTER tpl_cyd_display_init() — the 3.5" board
 * shares the display SPI bus, and the SPI must be routed first.
 * On 2.8" boards (private SPI), the order is irrelevant.
 * 
 * @param touchscreen Reference to XPT2046_Touchscreen object (must be constructed)
 * @param rotation Must match the display rotation passed to tpl_cyd_display_init()
 * 
 * Sets up the SPI bus (private or shared) and calls setRotation().
 * For 3.5" boards, reuses TFT_eSPI's SPI instance (getSPIinstance()).
 * 
 * Returns void. On 3.5" boards, calls tpl_cyd_display_init() first
 * with an assert/serial message if that hasn't happened yet.
 */
void tpl_cyd_touch_init(XPT2046_Touchscreen &touchscreen, uint8_t rotation);

/**
 * Read touch data and map to screen coordinates in the requested orientation.
 * 
 * This is the core of the abstraction: it handles the full rotation pipeline
 * internally, so project code is rotation-agnostic.
 * 
 * Pipeline:
 *   1. Calls touchscreen.getPoint() for raw ADC values
 *   2. Applies XPT2046 library rotation transform
 *   3. Maps rotated ADC to portrait screen coordinates using calibration ranges
 *   4. Applies the requested logical rotation
 *   5. Returns coordinates in the requested orientation, ready for LVGL
 * 
 * @param touchscreen Reference to XPT2046_Touchscreen object
 * @param data Pointer to lv_indev_data_t (data->point will be set)
 * @param pressed Set to true if touch detected, false if released
 * @param screen_w Screen width (portrait, from CYD_SCREEN_WIDTH)
 * @param screen_h Screen height (portrait, from CYD_SCREEN_HEIGHT)
 * @param rotation Display rotation (0–3, must match display init)
 */
void tpl_cyd_touch_read(XPT2046_Touchscreen &touchscreen,
                        lv_indev_data_t *data, bool *pressed,
                        int32_t screen_w, int32_t screen_h,
                        uint8_t rotation);

/**
 * Initialize RGB LED pins for CYD boards.
 * 
 * For 2.8" boards, configures tpl_ledPin_R/G/B (4/16/17) as outputs
 * and sets them HIGH (inverted logic → LEDs off).
 * For 3.5" boards, sets pins 4/16/17 as outputs and pulls them HIGH
 * to prevent floating input state.
 * 
 * Same pin state (HIGH), different hardware reasons — works for both.
 * 
 * @param turn_off_rgb true to set all RGB pins to OFF state
 */
void tpl_cyd_rgb_init(bool turn_off_rgb);

/**
 * Read the CDS (Cadmium Sulfide) light sensor on 3.5" CYD boards.
 * 
 * Only meaningful on 3.5" boards — the 2.8" board has no CDS component.
 * Returns 0–4095 from analogRead(CYD_CDS_PIN).
 * 
 * @return ADC value (0–4095), or 0 if not on a 3.5" board
 */
uint16_t tpl_cyd_light_sensor_read(void);
```

### 4.3 platformio.ini Changes

Each CYD project's `platformio.ini` adds one define:

```ini
# For 2.8" CYD projects (e.g., 26_CYD_CAN_monitor esp32ota/esp32usb envs)
build_flags =
  ...
  -D IS_ESP32_CYD_28

# For 3.5" CYD projects (e.g., 26_CYD_CAN_monitor esp32_35_usb env)
build_flags =
  ...
  -D IS_ESP32_CYD_35
```

**Removed from platformio.ini:** All per-board TFT pin defines (`TFT_MISO`, `TFT_MOSI`, etc.), all `TOUCH_X_MIN/MAX`, `TOUCH_Y_MIN/MAX`, `TFT_WIDTH`, `TFT_HEIGHT`, `TFT_ROTATION`, driver selection (`ILI9341_2_DRIVER`, `ST7796_DRIVER`), `ESP32_35_BOARD`, and non-font TFT_eSPI defines (`USER_SETUP_LOADED`, `SPI_FREQUENCY`, `SPI_READ_FREQUENCY`, `SPI_TOUCH_FREQUENCY`, `TFT_INVERSION_ON`, `TFT_BACKLIGHT_ON`, `TFT_RGB_ORDER`).

**Retained in platformio.ini:** Font defines (`LOAD_FONT2`, `LOAD_FONT4`, `LOAD_FONT6`, `LOAD_FONT7`, `LOAD_FONT8`, `SMOOTH_FONT`, `LOAD_GLCD`) — these are project-specific and cannot be moved into C++ headers. Also retained: project-specific pin defines (e.g., CAN TX/RX via connector), and project-specific flags (`LV_LVGL_H_INCLUDE_SIMPLE`, etc.).

Non-font TFT_eSPI defines now live in `tpl_display.h` based on the board define.

### 4.4 main.cpp Changes

Before (Project 26, ~80 lines of display/touch code):

```cpp
// Display constants
#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 3
#endif
// ... 20+ more #ifndef defaults ...
// ... manual SPI setup ...
// ... touch mapping with rotation switch ...
```

After (Project 26, ~10 lines):

```cpp
#include "tpl_display.h"  // New template module

// In setup():
TFT_eSPI tft = TFT_eSPI();          // Global (as before)
lv_display_t *disp = tpl_cyd_display_init(TFT_ROTATION);
tpl_cyd_touch_init(touchscreen, TFT_ROTATION);
tpl_cyd_rgb_init(true);   // turn off floating RGB pins

// In loop():
// Touch callback — the abstraction handles the full rotation pipeline:
static void touchscreen_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    bool pressed;
    tpl_cyd_touch_read(touchscreen, data, &pressed,
                       CYD_WIDTH, CYD_HEIGHT, TFT_ROTATION);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
```

---

## 5. Integration with Existing Template

### 5.1 `tpl_system.h`

The template's `tpl_system.h` **already provides** correct board-specific pin defines via `IS_ESP32_CYD_28` / `IS_ESP32_CYD_35`. No changes needed in the template — only project `platformio.ini` files need updating. Projects replace `-D IS_ESP32_CYD_28` (or `IS_ESP32_CYD_35`) in their `platformio.ini`.

### 5.2 `template.h` Integration

`tpl_display.h` will be added to `template.h` alongside all other template modules:

```cpp
#include "tpl_display.h"  // CYD display/touch abstraction
```

### 5.3 Build Flag Precedence

```
tpl_display.h defaults   (board-specific constants — final)
  ↓ (falls through to)
TFT_eSPI library defaults (library User_Setup.h)
```

Font defines remain in `platformio.ini` (project-specific). All other TFT_eSPI defines are in `tpl_display.h`.

---

## 6. Comparison: Current vs. Proposed

| Aspect | Current (Project 19) | Current (Project 26) | Proposed (tpl_display) |
|--------|---------------------|---------------------|----------------------|
| Board detection | `IS_ESP32CYD` (generic) | `ESP32_35_BOARD` (implicit 2.8" default) | `IS_ESP32_CYD_28` or `IS_ESP32_CYD_35` (explicit) |
| Screen dims | Hardcoded in `main.cpp` | `#ifndef` defaults + `platformio.ini` | `tpl_display.h` (board-specific, final) |
| Touch calibration | Hardcoded `map()` params in `main.cpp` | `platformio.ini` per-env | `tpl_display.h` defaults (final) |
| Rotation | Hardcoded `setRotation(3)` | `platformio.ini` per-env + code | `tpl_cyd_touch_read()` handles full pipeline |
| Touch SPI (2.8″) | Private `SPIClass` | Private `SPIClass` | `tpl_display.h` (board-specific) |
| Touch SPI (3.5″) | N/A (no 3.5″ support) | Shares display SPI | `tpl_display.h` (board-specific) |
| Backlight pin | `tpl_backlightPin` (21) | `TFT_BL` (27) for 3.5″, 21 for 2.8″ | `tpl_display.h` (board-specific) |
| RGB LED | `tpl_ledPin_R/G/B` (4/16/17) | 3.5″: explicit pins 4/16/17 HIGH | `tpl_cyd_rgb_init()` (both boards) |
| Light sensor | Not handled | Not handled | `tpl_cyd_light_sensor_read()` (3.5″ only) |
| All pins declared | No (scattered) | No (scattered) | Yes (Section 4.2.3) |
| Code duplication | ~60 lines display/touch in `main.cpp` | ~80 lines display/touch in `main.cpp` | ~5 lines in `main.cpp` |
| Add new CYD board | Edit `main.cpp` + `platformio.ini` | Edit `main.cpp` + `platformio.ini` | Edit `tpl_display.h` only |

---

## 7. Implementation Plan

### Phase 1: Create `tpl_display.h` / `tpl_display.cpp` in `prj_template`

1. Define board constants (dimensions, driver, all pins, calibration defaults).
2. Implement `tpl_cyd_display_init()` — configures global `TFT_eSPI tft` with board-specific settings, returns LVGL display handle.
3. Implement `tpl_cyd_touch_init()` — initializes touch SPI (private or shared), enforces display-first ordering with assert/serial message.
4. Implement `tpl_cyd_touch_read()` — full rotation pipeline (ADC → portrait → logical rotation), returns coordinates in requested orientation.
5. Implement `tpl_cyd_backlight_init()` — LEDC PWM setup.
6. Implement `tpl_cyd_rgb_init()` — RGB LED pin configuration (HIGH works for both boards: inverted LEDs vs. floating pins).
7. Implement `tpl_cyd_light_sensor_read()` — analog read of CDS (0–4095), returns 0 on 2.8″ boards.

### Phase 2: Update `template.h`

1. Add `#include "tpl_display.h"` to the template header.

### Phase 3: Update Project 26 (3.5″ environment)

1. Replace `#if defined(ESP32_35_BOARD)` with `#if defined(IS_ESP32_CYD_35)` everywhere (one-step migration, no backward compat).
2. Remove all per-environment TFT/touch defines from `platformio.ini` (non-font TFT_eSPI defines move to `tpl_display.h`; font defines stay).
3. Replace `main.cpp` display/touch code with `tpl_display` calls.
4. Test on 3.5″ hardware.

### Phase 4: Update Project 26 (2.8″ environments)

1. Add `IS_ESP32_CYD_28` to `esp32ota` and `esp32usb` envs.
2. Remove all per-environment TFT/touch defines from `platformio.ini`.
3. Replace `main.cpp` display/touch code with `tpl_display` calls.
4. Test on 2.8″ hardware.

### Phase 5: Update Project 19

1. Replace `-D IS_ESP32CYD` with `-D IS_ESP32_CYD_28` in all environments.
2. Remove all per-environment TFT/touch defines from `platformio.ini`.
3. Replace `main.cpp` display/touch code with `tpl_display` calls.
4. Remove `include/tft_setup.h` (duplicated TFT_eSPI User_Setup.h).
5. Test on 2.8″ hardware.

### Phase 6: Documentation

1. Update `AGENTS.md` with CYD board abstraction section.
2. Add `tpl_display.h` documentation to `prj_template/README`.
3. Add a CYD starter project (`27_cyd_starter`) demonstrating the abstraction.

---

## 8. Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Touch calibration values differ between individual boards | **High** (manufacturing variation) | `tpl_display.h` provides board-specific defaults. If a board's calibration differs, edit `tpl_display.h` (one file). |
| 3.5″ board shares SPI bus causing conflicts | **Medium** | `tpl_cyd_touch_init()` enforces display-first ordering. On 3.5″ boards, calls `TFT_eSPI::getSPIinstance()` for shared bus. Asserts with serial message if called before display init. |
| Backlight PWM channel conflicts | **Low** | Use channel 7 (already used by both projects). Document the choice. |
| RGB LED pin conflicts on 3.5″ board | **Low** | `tpl_cyd_rgb_init()` handles both boards: 2.8″ configures pins as outputs (inverted logic → HIGH = off); 3.5″ pulls them HIGH (prevents floating). Same state, different reasons. |
| Project 19's `tft_setup.h` duplication | **Medium** | Phase 5 removes the duplicated `include/tft_setup.h` file entirely — all non-font TFT_eSPI defines are handled by `tpl_display.h`. Font defines remain in `platformio.ini`. |

---

## 9. Future Extensions

### 9.1 Additional Board Support

Adding a new CYD variant (e.g., 4.0″ GC9A01) requires only:

1. Add a new `#elif defined(IS_ESP32_CYD_40)` block in `tpl_display.h`.
2. Define its dimensions, driver, all pins, calibration ranges, and backlight pin.
3. Add `-D IS_ESP32_CYD_40` to the project's `platformio.ini`.

No changes to any `main.cpp` file.

### 9.2 Touch Calibration Tool

A serial command could measure and report touch calibration ranges:

```cpp
// Command: /touch/calibrate
// Output: {"x_min":200,"x_max":3700,"y_min":240,"y_max":3800}
```

This would be a convenience feature in `tpl_display.cpp` — users move a crosshair to screen corners and the tool reports the ADC ranges, which are then used to update `tpl_display.h` for that board variant.

### 9.3 Rotation Command

A serial command to change rotation without reboot:

```cpp
// Command: /display/rotate 0|1|2|3
// Effect: Calls lv_display_set_rotation() and touchscreen.setRotation()
```

---

## 10. Open Questions (Resolved)

### Q1: `TOUCH_INVERTED` flag — **Resolved: Automatic per-board**

Project 19 (2.8″) defines `-D TOUCH_INVERTED` in `platformio.ini`; Project 26 (both 2.8″ and 3.5″ envs) does not. The 2.8″ board needs touch inversion; the 3.5″ board does not.

**Decision:** Set it automatically per-board in `tpl_cyd_touch_init()` based on the board define. The 2.8″ board enables `TOUCH_INVERTED` in the XPT2046 constructor; the 3.5″ board does not.

### Q2: `TFT_INVERTED` / `TFT_INVERSION_ON` flag — **Resolved: Automatic per-board**

Project 19 (2.8″) uses `-D TFT_INVERTED` (older name); Project 26 (2.8″ envs) uses `-D TFT_INVERSION_ON` (newer name, same effect). The 3.5″ ST7796 env does **not** define either — the 3.5″ panel doesn't need color inversion.

**Decision:** Set `TFT_INVERSION_ON` automatically per-board in `tpl_display.h`. The 2.8″ ILI9341 panel needs color inversion; the 3.5″ ST7796 does not. Use the modern name `TFT_INVERSION_ON` consistently.

### Q3: `USE_HSPI_PORT` flag — **Resolved: Remove entirely**

Both projects define `-D USE_HSPI_PORT` in `common/build_flags`. However, the actual pin numbers (`TFT_MOSI=13, TFT_SCLK=14, TFT_MISO=16`) are **VSPI pins**, not HSPI pins (which would be 18/19/23). The flag is **dead code** — custom pin defines override the peripheral selection in TFT_eSPI.

**Decision:** The abstraction does **not** define `USE_HSPI_PORT`. Both CYD boards use VSPI pins. Both projects' `platformio.ini` files should drop this flag during migration.

### Q4: SD card integration — **Resolved: Document, no extra abstraction**

Both projects use `tpl_sd` module. The `tpl_sd` module uses `SPIClass(VSPI)` (confirmed in `tpl_sd.cpp`). On the 2.8″ board, the touch uses a **separate VSPI bus** (MOSI=32, MISO=39, CLK=25) with its own CS (33), so no conflict. On the 3.5″ board, the touch **shares the display VSPI** (MOSI=13, MISO=12, SCLK=14) but has its own CS (33), and the SD card uses the same VSPI with its own CS — all three devices coexist because each has a different CS pin.

**Decision:** Document that all three devices (display, touch, SD) share VSPI and coexist via different CS pins. No additional abstraction needed.

### Q5: Draw buffer sizing — **Resolved: Provide a macro**

The LVGL draw buffer size depends on resolution: `DRAW_BUF_SIZE = (W × H / 10) × (LV_COLOR_DEPTH / 8)`. For 2.8″ (240×320): ~15 KB. For 3.5″ (320×480): ~37.5 KB.

**Decision:** Provide a macro in `tpl_display.h`:
```cpp
#define CYD_DRAW_BUF_SIZE (CYD_SCREEN_WIDTH * CYD_SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
```

### Q6: `tpl_cyd_display_init()` return value — **Resolved: Global `TFT_eSPI tft` stays in project code**

The function returns `lv_display_t*`. The global `TFT_eSPI tft` object must be declared in project code (as it is now). The abstraction configures it but does not own it — LVGL's driver hardcodes `extern TFT_eSPI tft;`.

**Decision:** Projects declare `TFT_eSPI tft = TFT_eSPI();` as before. `tpl_cyd_display_init()` configures it and returns the LVGL handle.

### Q7: `IS_ESP32CYD` removal — **Resolved: Already obsoleted in template**

`IS_ESP32CYD` has been obsoleted in the template. `tpl_system.h` already provides correct board-specific pin defines via `IS_ESP32_CYD_28` / `IS_ESP32_CYD_35`.

**Decision:** Remove `-D IS_ESP32CYD` from projects 19 and 26's `platformio.ini` files and replace with `-D IS_ESP32_CYD_28` (or `IS_ESP32_CYD_35` for the 3.5″ env). No backward-compat alias needed.

### Q8: Call ordering (display before touch) — **Resolved: Enforce in code**

On 3.5″ boards, the touch shares the display SPI bus. `tpl_cyd_touch_init()` checks an internal flag set by `tpl_cyd_display_init()`. If called too early, it asserts with a serial message: "Call tpl_cyd_display_init() before tpl_cyd_touch_init()". On 2.8″ boards (private SPI), the order is irrelevant.

### Q9: Non-LVGL use case — **Resolved: No abstraction needed**

No display = no `IS_ESP32_CYD_*` define = no `tpl_display.h` included. Projects without a CYD board manage their own hardware.

### Q10: Calibration override — **Resolved: Not needed**

Calibration defaults in `tpl_display.h` are final. If a board's calibration differs, edit `tpl_display.h` (one file). No project-level override mechanism.

### Q11: Backward compat (`ESP32_35_BOARD`) — **Resolved: None needed**

Replace `#if defined(ESP32_35_BOARD)` with `#if defined(IS_ESP32_CYD_35)` everywhere in a single migration step. No backward-compat aliases.

### Q12: Speaker/CDS — **Resolved: Constants + optional helper**

`CYD_SPEAKER_PIN 26` and `CYD_CDS_PIN 34` are declared as constants in `tpl_display.h` (for projects that need them). `tpl_cyd_light_sensor_read()` provides an optional helper for reading the CDS (Cadmium Sulfide) photoresistor on 3.5″ boards. Speaker init can be added later if needed.

---

## 11. Conclusion

The CYD board abstraction layer in `prj_template` eliminates **all** hardcoded display/touch configuration from individual project `main.cpp` files. Board-specific constants (dimensions, driver, **all** pins, calibration defaults) move to `tpl_display.h`.

The abstraction's main value: `tpl_cyd_touch_read()` handles the **full rotation pipeline** internally (ADC → XPT2046 rotation → calibration mapping → logical rotation), so project code is rotation-agnostic. The touch callback becomes a single function call.

The existing `IS_ESP32CYD` flag has been obsoleted in the template. Project `platformio.ini` files are updated to use `IS_ESP32_CYD_28` / `IS_ESP32_CYD_35`.

The result: adding a new CYD variant requires editing **one file** (`tpl_display.h`) and adding **one define** to `platformio.ini`. No `main.cpp` changes are needed.

---

## Appendix A: Full Board Constant Reference

### 2.8″ CYD (SUNTON 2432S028R)

```cpp
// tpl_display.h constants (IS_ESP32_CYD_28)
// Display SPI
CYD_TFT_MOSI    13
CYD_TFT_MISO    16
CYD_TFT_SCLK    14
CYD_TFT_CS      15
CYD_TFT_DC       2
CYD_TFT_RST     12

// Touch (dedicated SPI bus)
CYD_TOUCH_MOSI   32
CYD_TOUCH_MISO   39
CYD_TOUCH_CLK    25
CYD_TOUCH_CS     33
CYD_TOUCH_IRQ    36

// Screen
CYD_SCREEN_WIDTH    240
CYD_SCREEN_HEIGHT   320
CYD_DRIVER          ILI9341_2_DRIVER

// Touch calibration (ADC ranges after XPT2046 rotation transform)
CYD_TOUCH_X_MIN     200
CYD_TOUCH_X_MAX    3700
CYD_TOUCH_Y_MIN     240
CYD_TOUCH_Y_MAX    3800

// Backlight
CYD_BACKLIGHT_PIN  21

// RGB LED (inverted logic)
CYD_RGB_RED        4
CYD_RGB_GREEN     16
CYD_RGB_BLUE      17

// BOOT button
// CYD_BOOT_PIN  0  // (from tpl_system.h when board define set)
```

### 3.5″ CYD (SUNTON 3248S035R)

```cpp
// tpl_display.h constants (IS_ESP32_CYD_35)
// Display SPI
CYD_TFT_MOSI    13
CYD_TFT_MISO    12   // differs from 2.8"
CYD_TFT_SCLK    14
CYD_TFT_CS      15
CYD_TFT_DC       2
CYD_TFT_RST     -1   // no reset pin

// Touch (shares display SPI)
CYD_TOUCH_CS     33
CYD_TOUCH_IRQ    36

// Screen
CYD_SCREEN_WIDTH    320
CYD_SCREEN_HEIGHT   480
CYD_DRIVER          ST7796_DRIVER

// Touch calibration (ADC ranges after XPT2046 rotation transform)
CYD_TOUCH_X_MIN     180
CYD_TOUCH_X_MAX    3800
CYD_TOUCH_Y_MIN    3900  // inverted relative to panel
CYD_TOUCH_Y_MAX     220

// Backlight
CYD_BACKLIGHT_PIN  27   // differs from 2.8"

// Speaker (3.5″ only)
CYD_SPEAKER_PIN  26   // DAC2 / I2S

// CDS light sensor (3.5″ only)
CYD_CDS_PIN  34   // analog input (Cadmium Sulfide photoresistor)

// RGB LED (floating, must be pulled HIGH)
CYD_RGB_RED        4
CYD_RGB_GREEN     16
CYD_RGB_BLUE      17

// BOOT button
// CYD_BOOT_PIN  0  // (from tpl_system.h when board define set)
```

---

## Appendix B: Current Code Locations

| File | Project | Content |
|------|---------|---------|
| `19_CYD-project/src/main.cpp` | 19 | Hardcoded `SCREEN_WIDTH/HEIGHT`, `map()` params, `setRotation(3)`, manual SPI setup, touch callback with rotation switch |
| `19_CYD-project/platformio.ini` | 19 | TFT pin defines, touch calibration, driver selection, rotation, `IS_ESP32CYD` |
| `19_CYD-project/include/tft_setup.h` | 19 | Full TFT_eSPI User_Setup.h (duplicated) |
| `26_CYD_CAN_monitor/src/main.cpp` | 26 | `#ifndef` defaults, conditional SPI setup, touch callback with rotation switch, 3.5″ board handling |
| `26_CYD_CAN_monitor/platformio.ini` | 26 | Per-environment TFT pin defines, touch calibration, driver selection, rotation, `ESP32_35_BOARD`, `IS_ESP32CYD` |
| `prj_template/include/tpl_system.h` | template | `IS_ESP32_CYD_28` / `IS_ESP32_CYD_35` pin defines (already correct) |
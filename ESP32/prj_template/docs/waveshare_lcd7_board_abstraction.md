# Waveshare ESP32-S3-Touch-LCD-7 board abstraction (`tpl_ws7`)

Extracted from `ESP32/28_hauszentrale/docs/whitepaper.md` §6. The 6.x section
numbers are preserved so existing references still resolve.

## 6. Deliverable A — `tpl_waveshare_lcd7` in `prj_template`

### 6.1 Why a new module and not an extension of `tpl_display`

Extending `tpl_display` would be the smaller diff and is the wrong move.

| Aspect | `tpl_display` (CYD) | Waveshare LCD-7 | Same abstraction? |
| ------ | -------------------- | --------------- | ---------------- |
| Panel bus | SPI, TFT_eSPI owns it | RGB parallel, LCD peripheral owns it | **No** — different driver stacks, different flush model |
| Panel IO | 6 GPIOs | 20 GPIOs incl. a generated PCLK | No |
| Touch | XPT2046, resistive, own SPI | GT911, capacitive, I2C | No — calibration model is entirely different |
| Peripheral enables | Direct GPIO + LEDC | I2C expander, active-low CS, no PWM | No |
| Colour inversion | Board-dependent quirk to work around | Not applicable (RGB panel) | No |

The only shared surface is the *shape* of the API — init display, init touch,
read touch with logical coordinates, backlight, set default display. That shape is
what projects program against, so it is what is worth reusing; the mechanism
behind it is not. `tpl_display` stays untouched and CYD projects are unaffected.

What is genuinely shared and should be factored out is the **LVGL host
initialisation recipe** (`lv_init()` → create display → `lv_display_set_default()`
→ create indev → set read callback). That is a four-line sequence duplicated in
`19`, `26` and `27`, and it is a candidate for a tiny shared helper (§14).

### 6.2 Module API

The naming mirrors `tpl_display` so that a reader who knows one knows the other,
but the `tpl_cyd_` prefix becomes `tpl_ws7_` because these are not CYDs.

```cpp
// ---- lifecycle -----------------------------------------------------------
esp_err_t tpl_ws7_setup(uint8_t rotation);          // expander, panel, backlight, touch
lv_display_t *tpl_ws7_display(void *buf, size_t buf_size);  // uses the rotation from setup(); calls lv_display_set_default()
lv_indev_t   *tpl_ws7_touch(lv_display_t *disp);     // lv_indev_create() needs the display

// ---- geometry ------------------------------------------------------------
#define TPL_WS7_SCREEN_WIDTH   800    // native panel
#define TPL_WS7_SCREEN_HEIGHT  480
#define TPL_WS7_WIDTH          /* rotated logical width  */
#define TPL_WS7_HEIGHT         /* rotated logical height */

// ---- expander-backed signals (named; no raw register access) ---------------
esp_err_t tpl_ws7_backlight(bool on);                // CH422G IO2 → AP3032 EN
esp_err_t tpl_ws7_touch_reset_assert(void);         // CH422G IO1
esp_err_t tpl_ws7_touch_reset_release(void);
esp_err_t tpl_ws7_panel_reset(void);                // CH422G IO3

// ---- SD card chip select (active LOW on the expander) --------------------
esp_err_t tpl_ws7_sd_select(bool select);            // IO4; true drives it LOW
SPIClass    *tpl_ws7_sd_bus(void);                   // the SPI the module started: MOSI 11, SCLK 12, MISO 13

// ---- CAN / USB mux -------------------------------------------------------
esp_err_t tpl_ws7_set_usb_mode(bool usb);           // IO5; usb=true -> CAN disabled
bool     tpl_ws7_can_available(void);                // IO5 high?
```

There is no `tpl_ws7_gpio19_20_mode()`. On the ESP32-S3, GPIO 19/20 are the native
USB pins, and USB-Serial-JTAG or USB-OTG claims them only while that peripheral is
initialised. There is no IOMUX switch to flip at runtime. The decision is
compile-time: `ARDUINO_USB_CDC_ON_BOOT=0` and `ARDUINO_USB_MODE=0` in §7.1, and no
call to `HWCDC` / `USB.begin()` anywhere in this project. TWAI then takes GPIO
19/20 as ordinary GPIOs. Q2 is that choice, confirmed against the CH9102 bench
(§3.1.1).

`rotation` is an argument of `setup()` only. `display()` reads the value `setup()`
stored. Passing it twice was a way to get the touch layer mirrored relative to the
panel.

### 6.3 Display pipeline (RGB panel + LVGL)

Driver stack: `esp_lcd_rgb_panel` (or the board's `esp_lcd` RGB panel component)
→ `esp_lcd_panel_io_additions` → LVGL 9.2 `lv_display_create` with a custom flush
callback that calls `esp_lcd_panel_draw_bitmap()`.

Decisions:

- **`LV_COLOR_DEPTH = 16` (RGB565).** 16-bit is what the panel takes; 32-bit would
  double the bandwidth for no visible gain and eat the PSRAM headroom.
- **`LV_COLOR_16_SWAP = 0`.** RGB565 is sent big-endian (high byte first) per the
  panel spec; LVGL's default byte order already matches. Left at the default
  deliberately — no `LV_COLOR_16_SWAP` anywhere in this project.
- **Draw buffer: two line buffers, not a full-screen buffer.** With RGB panels the
  flush is a DMA transfer and the driver can write back into the buffer it just
  flushed, so LVGL's `LV_DISPLAY_RENDER_MODE_DIRECT` with a pair of
  **panel-width** strips is the memory-efficient choice. The strip is 800 px
  (the panel), not 480 px (the rotated logical width). For 800 × 20 px at
  RGB565 that is 32 000 B per buffer in internal RAM — it must *not* be in PSRAM,
  because the DMA cannot cross the PSRAM boundary on an octal PSRAM part.
  The buffer size is a `-D` flag (`TPL_WS7_DRAW_LINES`, default 20) so the S3's
  512 KB of internal SRAM is spent deliberately. This is the opposite of the CYD
  boards, where a tenth-of-a-screen PSRAM buffer is fine.
- **Timing** defaults to 12 MHz pixel clock in this project (`TPL_WS7_PCLK_HZ`;
  the template default is 16 MHz), 4/8/8 horizontal and vertical
  pulse/back/front values, `hsync_idle_low`/`vsync_idle_low` off (active high),
  `de_idle_high` off, pixel clock idle high. Every value is a `TPL_WS7_*`
  macro with an `#ifndef` guard, so it can be overridden in `platformio.ini`
  without editing the template. Wider porches were tried and made the OTA
  stripes worse, so 4/8/8 is the shipped default.
- **`RGB_BOUNCE_LINES = 10`** and `RGB_TRAINING_LINES = 16` are kept from the
  Waveshare reference; the latter matters because the first lines after a reset
  are not valid on this panel.

**Known issue — vertical stripes after OTA.** On this panel an OTA reboot can
leave the RGB DMA/panel out of sync, visible as vertical stripes or a shifted
image. The temporary remedy is **Settings → PANEL RESET** on the touchscreen:
`tpl_ws7_reset_rgb_timing()` deletes and recreates the ESP32 RGB panel from the
compile-time `TPL_WS7_*` timing values and pulses the ST7262 reset through CH422G
IO3. It keeps the LVGL display and does not erase history or configuration. A
power cycle is the fallback if the screen is too corrupted to reach Settings.
This is a workaround, not a root-cause fix; the 4/8/8 timing and 12 MHz project
clock reduce the incidence but do not eliminate it.

### 6.4 Touch pipeline (GT911)

The GT911 comes up through `esp_lcd_touch_gt911`, on the same I²C port as the
CH422G, at `0x5D` (the address the board README's map marks; the other legal
address is `0x14`). Reset and interrupt are not ESP32 GPIOs:

```cpp
// rst_gpio_num = -1    reset is CH422G IO1, sequenced by tpl_ws7_touch_reset_*()
// int_gpio_num = -1    no INT line is broken out; the controller is polled
```

The field names above are the component's. An earlier draft of this section
invented `control_panel_io` and `esp_lcd_touch_panel_io_i2c_config_t`. Those are
not in `esp_lcd`. Phase 3 copies the struct from the version of
`esp_lcd_touch_gt911` that Q21's core actually resolves.

The interesting consequence: **the GT911 has neither a reset line nor an interrupt
line on an ESP32 GPIO.** Both are routed through the CH422G, and the reference
configuration reads the touch controller by polling. That means:

- `tpl_ws7_touch_reset_*()` must run before the controller is addressed, and the
  I2C bus must tolerate the controller's NACK while it is held in reset.
- The read callback must tolerate `ESP_ERR_TIMEOUT` from the GT911 as the normal
  "no finger" case, not as an error worth logging. `26_CYD_CAN_monitor` has the
  same problem in a milder form with the XPT2046 and solves it with a 40 ms
  debounce; here the poll itself is the mechanism, so the debounce moves into the
  module.
- Coordinate space is already panel-native — no ADC calibration table, no
  `map()`-and-`constrain()` dance. **Portrait is therefore cheap**: with
  `LV_DRAW_TRANSFORM_USE_MATRIX = 1` and `lv_display_set_rotation(disp,
  LV_DISPLAY_ROTATION_90)`, LVGL rotates during rendering and the panel is never
  re-programmed. The GT911's reported coordinates rotate with the same constant,
  so no touch remap function is needed at all.

This is the one place where the Waveshare board is *simpler* than the CYDs, and the
module should not pretend otherwise by carrying a dead calibration code path.

### 6.5 CH422G Ownership Model

**Built on `esp_io_expander`, not on hand-rolled register pokes.** Espressif ships
an official component for exactly this, and it is what the Waveshare examples
already use (`boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/source/esp_io_expander_ch422g.{c,h}`,
`esp_expander_ch422g.hpp`). It supplies the three things that were previously going
to be re-derived badly:

| Need | Provided by the component |
| ---- | ------------------------- |
| The device's own pin map | documented in `esp_expander_ch422g.hpp` — IO0–IO7 plus OC0–OC3 as pins 8–––11 |
| Direction control | `enableAllIO_Output()` / `enableAllIO_Input()`, which correctly handles the fact that IO0–IO7 direction is **all-or-nothing** |
| Open-drain vs push-pull | `enableOC_PushPull()` / `enableOC_OpenDrain()` — typed, named, and exactly the choice SD_CS needs |
| Writing several pins at once | `Base::multiSetIO()`, one transaction for a set of pins |

That last one matters more than it looks. A per-pin `setIO()` is a read-modify-write
over I²C — **two transactions per named setter** — on a bus the GT911 poll and the
sensor task also contend for (N13). Rev 1.2's "must not go through `esp_io_expander`"
was too strong; the component is the right foundation and the transaction cost is
avoidable *within* it by batching.

**What the module adds on top is policy, not mechanism.** Three layers:

```
  esp_io_expander / esp_expander::CH422G     mechanism: pin map, direction, OC mode
        |
  tpl_waveshare_lcd7                          policy: shadow, mutex, named setters
        |
  project code                                never sees a pin number
```

1. **A cached shadow of the 12-bit pattern.** The component re-reads before every
   write; the module holds the pattern itself, so `tpl_ws7_set_usb_mode()` is a
   **single** I²C write and cannot lose an update to another task's concurrent
   read-modify-write. This is the one thing the module genuinely needs that the
   component does not provide.
2. **The one bus mutex from N13, taken with a bounded timeout.** Not a second
   lock. The expander, the GT911, the SHT45 and the SD chip-select all share
   GPIO 8/9; two mutexes would deadlock the moment the touch poll and a setter
   nest. The touch poll is the caller that may not wait (§10.1).
3. **Named, board-meaningful setters.** Projects call `tpl_ws7_sd_select(true)`,
   never `setIO(4, …)`. The pin numbers live in one place, and §3.4's table is what
   that place is derived from.

**Two traps in the component that the module must document rather than fix:**

- **`write_output_reg()` skips a zero byte.** The CH422G driver writes WR-OC and
  WR-IO only `if (the computed byte)` is non-zero, so a pattern that computes to
  `0x00` is silently not sent. The design never needs an all-zero pattern (every
  named signal has a defined idle level — SD_CS idles *high*), but the module must
  **assert the pattern is non-zero** before calling, so the assumption is checked
  rather than remembered. Phase 1's exit criterion includes a **read-back test** of
  every named setter, because "the write was skipped" is otherwise invisible.
- **`begin()` leaves IO0–IO7 output HIGH.** USB_SEL is IO5, so the component's
  documented default puts the board in **CAN mode with USB dead**. This is not an
  edge case — it is the documented behaviour of the library we are building on,
  and it is why §7.12's boot sequence (N19) is not optional politeness. See §3.3.

**The invariant, in a comment in the header:** *after bring-up, `USB_SEL` (IO5) is
the only bit any project may want to change, and the CAN path must always be able to
put it back.*

### 6.6 Portrait orientation

Portrait is the only supported orientation for this project, but the module
supports 0–3 because it costs one macro and keeps the abstraction honest.

| rotation | logical size | panel | note |
| -------- | ------------ | ----- | ---- |
| 0 | 480 × 800 | 800 × 480 native | portrait. LVGL `LV_DISPLAY_ROTATION_90`, **not** `LV_DISPLAY_ROTATION_0` |
| 1 | 800 × 480 | 800 × 480 native | landscape, LVGL rotation 0, no transform |
| 2 | 480 × 800 | 800 × 480 | portrait, 180° (LVGL rotation 270) |
| 3 | 800 × 480 | 800 × 480 | landscape, mirrored (LVGL rotation 180) |

`TPL_WS7_ROTATION` is this table, not the LVGL enum. Passing `0` to
`lv_display_set_rotation()` would leave the panel in landscape. `tpl_ws7_display()`
is the only place that translates.

`TPL_WS7_WIDTH`/`TPL_WS7_HEIGHT` are defined from `TPL_WS7_ROTATION` so that UI
code writes `TPL_WS7_WIDTH` and gets the truth.

### 6.7 Integration points in `template.h`, `tpl_system.h`, `tpl_sd`

**`template.h`** — add `#include "tpl_waveshare_lcd7.h"`. The header is guarded
internally by `#if defined(IS_WAVESHARE_LCD7)` so non-Waveshare projects — which is
all of the existing ones — pull in nothing and link unchanged. Same discipline as
`tpl_display.h`.

**`tpl_system.h`** — add a block that defines **no** LED pins:

```cpp
#ifdef IS_WAVESHARE_LCD7
// No status LED is fitted (the board README's onboard list is BOOT, RESET,
// battery, USB-C, the PH2.0 headers and CAN termination).
// Do not #define tpl_ledPin, tpl_ledPin_R/G/B or tpl_backlightPin:
//   GPIO 2  is RGB DATA12. A backlight alias there corrupts a whole colour bit.
//   GPIO 17 is RGB DATA3.  That is tpl_ledPin_B on both CYD boards.
//   GPIO 16 is tpl_ledPin_G on both CYD boards and is not an LED here.
//   GPIO 4  is outside the RGB/I²C/SD/CAN/UART map. It stays unclaimed so Q17
//           (CAN force-USB strap) and Q19 (LEDC backlight) can compete for it
//           without a template macro already driving it.
// tpl_wifi_setup() treats led pin 255 as "no LED" (tpl_wifi.cpp). Pass 255.
#endif
```

An undefined `tpl_backlightPin` is a compile error at the one call site that
names it. A `#define` of GPIO 2 is silent pixel corruption. The backlight setter
is `tpl_ws7_backlight()` (§6.2), which drives CH422G IO2.

**`tpl_sd`** — this needed a real change, not just a define. `tpl_sd` assumed a
directly-driven CS pin (`TPL_SD_CS`, default 5). Here the CS comes from the CH422G
and there is no GPIO at all. Options considered:

| Option | Verdict |
| ------ | ------- |
| A. Add a `TPL_SD_CS_VIA_EXPANDER` flag to `tpl_sd` | Rejected — pushes CH422G register knowledge into a generic module. |
| B. Give `tpl_sd` a typed backend (`SPIClass`, `SS` GPIO, `begin`/`end` callbacks) | **Chosen and implemented as `TplSdBackend`.** The project supplies the expander assert/release; `tpl_sd` stays board-agnostic. |
| C. New `recorder` service with its own SD backend; leave `tpl_sd` alone | Superseded by B. The recorder still owns writing, but mounting and browsing are shared. |

`tpl_sd` owns the mount and the *browser* (`/sd/list`, `/sd/read`, `/sd/download`,
`POST /sd/delete`); `/sd/status` stays project-specific. The recorder owns
*writing*. Both use the same physical card and the same recursive `tpl_sd_lock()`;
a failed lock wait defers a flush rather than declaring the card lost, and the card
is not held open across a webserver read. A custom backend skips the mount-time
FAT walk; the GPIO-CS backend still records sizes there. `/sd/read` is capped at
`TPL_SD_READ_MAX`, drops the lock before the body, and sends
`application/octet-stream`. `POST /spiffs/delete` is a separate `tpl_webserver`
route, not part of the SD backend. Both delete routes refuse traversal and the
firmware files (`/.sd_probe`, `/config.ini`, `/rollup.bin`, `/rollup_ring.bin`,
`/data_YYYYMMDD.bin`).

**The `.cpp` is guarded, not just the header.** `ESP32/Makefile` symlinks every
`prj_template/src/*.cpp` into every project. A new file therefore lands in
projects 01–27 the next time anyone runs `make links`. None of them define
`IS_WAVESHARE_LCD7`, so the translation unit is empty for them:

```cpp
#include "tpl_waveshare_lcd7.h"

#ifdef IS_WAVESHARE_LCD7
// ... entire implementation ...
#endif
```

The header guard already noted above is load-bearing for the same reason: the
Makefile also symlinks every template header into every project. Phase 1 does
not exit until `cd ESP32/26_CYD_CAN_monitor && pio run -e esp32ota` still passes
after both files exist.

**Symlinks.** `make links` regenerates `include/`, `src/`, `data/`, `tools/`
symlinks for every project; `tpl_waveshare_lcd7.{h,cpp}` are picked up by the
existing wildcard, which is why the guard has to be inside the file rather than
in the Makefile.

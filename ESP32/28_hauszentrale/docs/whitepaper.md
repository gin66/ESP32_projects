# White Paper: 28_hauszentrale — House Central on the Waveshare ESP32-S3-Touch-LCD-7

**Status:** COMPLETE — all sections written. Open questions in §12 remain to be answered before phase 1.
**Revision:** 1.1 — adds the living-room AHT20 + BMP280 local I²C sensor (§3.5, §7.8, F15–F17, N13, N14, §8.13)
**Last update:** 2026-10-16

---

## 0. Document Control

| Field | Value |
| ----- | ----- |
| Deliverable A | Board abstraction `tpl_waveshare_lcd7` inside `ESP32/prj_template` |
| Deliverable B | New project `ESP32/28_hauszentrale` |
| Board | WAVESHARE-ESP32-S3-Touch-LCD-7 (7", 800×480, ST7262, GT911) |
| Framework | PlatformIO / Arduino / ESP-IDF 5.x, LVGL 9.2 |
| CAN protocol | `include/haus_can.h` (repo root, single source of truth) |
| Local sensors | AHT20 (I²C `0x38`) + BMP280 (I²C `0x40`) on the H1 I²C header — living room |
| Predecessors | `26_CYD_CAN_monitor`, `19_CYD-project`, `15_power_log_eink` |
| Peer white paper | `ESP32/prj_template/docs/cyd_board_abstraction.md` |

**How to read this document.** Each chapter is written only once the underlying
design step has been closed. Open questions are collected in §12 rather than
being silently decided, and each one names who or what can answer it.

---

## 1. Problem Statement

The house already has a CAN bus (`include/haus_can.h`) carrying everything worth
putting on a wall: heater temperatures, PWM duty, ADC windows, EEPROM restore-point
health, bus statistics, a system-wide timestamp, and the SML smart-meter stream.
Two of those streams are *time series*, and today nothing in the house keeps a
history of them:

- `ESP32/15_power_log_eink` logs to an e-ink display but is a logger, not a display.
- `ESP32/26_CYD_CAN_monitor` shows decoded values live, per CAN ID, with no storage.
  It is a scope, not a panel — the answer to "what was" is not on it.

Three concrete gaps:

1. **No persistence.** Every number is live-only. A heater that stalls overnight
   leaves no trace; there is nothing to plot, nothing to export, nothing to compare
   a later fault against.
2. **No glanceable panel.** A 2.8"/3.5" CYD is too small for a piece of
   furniture-grade display, and the CAN monitor's table layout is the wrong shape
   for "how is the house doing right now".
3. **No single device that does both.** Bringing up history requires the logger
   project; reading it requires the monitor. They are separate boards, separate
   display technologies, and neither is a good wall panel.

The hardware to fix this already sits in the parts bin: the
WAVESHARE-ESP32-S3-Touch-LCD-7. It is a 7-inch 800×480 RGB panel with
capacitive touch, a CAN transceiver, **and an SD card slot on the same board** —
which is the combination the job needs. Its cost is that it is architecturally
unlike anything in `prj_template`: RGB parallel display instead of SPI, an I2C IO
expander (CH422G) fronting every peripheral enable, and a hardware multiplexer that
makes USB serial and CAN mutually exclusive.

So this project has two deliverables: teach `prj_template` how to drive this board,
then build the house central on top of it.

## 2. Scope

### 2.1 In Scope

**A — Template board support**

- New `tpl_waveshare_lcd7` module in `ESP32/prj_template` (`.h` + `.cpp`).
- RGB LCD panel bring-up (ST7262 via `esp_lcd_rgb_panel`), LVGL 9 flush driver.
- GT911 capacitive touch via `esp_lcd_touch_gt911`, mapped to logical portrait.
- CH422G ownership model: single init, named accessors for TP_RST / LCD_BL / SD_CS /
  USB_SEL, **not** a free-running GPIO namespace.
- Camera-less portrait rotation support.
- Board constants, `tpl_ledPin`/`tpl_backlightPin` wiring in `tpl_system.h`.
- Symlink regeneration + `make links` integration so every project can adopt it.

The board chapter of this document (§6) **is** the Deliverable-A white paper. When
the module is written it is split out to
`ESP32/prj_template/docs/waveshare_lcd7_board_abstraction.md`, following the
precedent of `cyd_board_abstraction.md`, and §6 is replaced by a pointer.

**B — Project `ESP32/28_hauszentrale`**

- `platformio.ini` with `esp32ota` + `esp32usb` envs, S3 / octal PSRAM / 8 MB flash.
- CAN (TWAI) client on the board's transceiver, 125 kbps, `haus_can.h` decoders.
- A **reusable** CAN monitor module lifted out of `26_CYD_CAN_monitor` (the scope
  view + detail overlay), reachable from the gear icon.
- SD card data recording: fixed-size binary log, retention/rotation.
- History charts at **1 day / 1 week / 1 month** on the detail screens.
- Home screen: background image, large clock without seconds, stylized house-heater
  graphic with the three temperatures, current power from CAN.
- Five tappable zones + a gear in the bottom-right corner (three heater
  temperatures, power, living-room climate).
- Detail screens with history charts and a back button.
- **Local climate sensor:** an AHT20 (temperature + humidity) and a BMP280
  (temperature + pressure) on the board's H1 I²C header, read directly by the
  panel — not on the CAN bus, and therefore outside the F14 resolution chain
  (§3.5, §7.8, §8.13).
- **On-panel diagnostics:** extend `tpl_debug_log` to a 1 MB PSRAM ring, plus a
  scrollable log screen with follow-at-end (§7.6, §8.12).
- Function resolution through sensor ID, never through CAN rank (F14, §7.7) — rank is
  shiftable address, the function belongs to the probe.
- OTA over WiFi (primary), with the USB/CAN mux consequence handled explicitly.

### 2.2 Out of Scope

- **Touch actuator control.** The panel is a display. Setting heater PWM from the
  wall panel is a separate project (and a separate safety discussion).
- **CAN transmit of heater commands.** This device listens. It is not a controller.
- **Redesigning `haus_can.h`.** Existing IDs stay; additions go in there repo-wide.
- **Telegram bot / Matter.** `tpl_template` modules that this board does not need.
- **Battery / power-budget engineering** for the PH2.0 cell connector. The board is
  assumed mains- or USB-powered; the cell is not a product feature here.
- **EEZ Studio round-tripping.** Screens are hand-authored LVGL, following the
  precedent that `26_CYD_CAN_monitor` already set when it outgrew its `.eez-project`.
- **LVGL theme authoring as a reusable template asset.** The theme ships inside the
  project, not in `prj_template`, because it encodes *house* semantics.

## 3. Hardware: Waveshare ESP32-S3-Touch-LCD-7

### 3.1 Identity

| Property | Value |
| -------- | ----- |
| Board | WAVESHARE-ESP32-S3-Touch-LCD-7 |
| MCU | ESP32-S3-WROOM-1U (U variant = external PSRAM/flash, no PCB antenna) |
| Flash / PSRAM | 8 MB quad flash, 8 MB **octal** PSRAM |
| Display | 7" TFT, **800 × 480**, RGB565 parallel, 16-bit data bus |
| Panel driver | ST7262 |
| Touch | GT911, capacitive, I2C, 5-point |
| IO expander | CH422G, I2C @ `0x24` |
| CAN | TJA1051 transceiver behind an FSUSB42UMX analog switch |
| Storage | microSD/TF slot, SPI, chip-select via CH422G |
| Extras | RS485 (external converter board), battery connector, BOOT button |

Full datasheets, schematic and community projects are catalogued in
`boards/WAVESHARE-ESP32-S3-Touch-LCD-7/`.

Two facts dominate every architectural decision that follows:

1. **The display is RGB parallel, not SPI.** It occupies 20 GPIOs and its pixel
   clock is generated by the LCD peripheral. `tpl_display` (TFT_eSPI + XPT2046)
   is structurally incapable of driving it.
2. **The board has a real CAN transceiver but hides the USB behind a switch.**
   The consequence is in §3.3 and it dictates the entire OTA story.

### 3.2 Resources and Pin Budget

Direct GPIOs consumed by the display alone: HSYNC 46, VSYNC 3, DE 5, PCLK 7, and
DATA0–DATA15 = 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40.

Everything else must fit in what remains, which is why the CH422G exists.

| Peripheral | Pins | Control |
| ---------- | ---- | ------- |
| Touch (GT911) | SDA 8, SCL 9 | I2C @ 400 kHz, shared with CH422G |
| CAN (TJA1051) | TX 19, RX 20 | behind FSUSB42UMX, gated by CH422G IO4 |
| SD (SPI) | MOSI 11, MISO 13, SCLK 12 | CS via CH422G IO3 |
| LCD reset | — | CH422G IO2 |
| Backlight | — | CH422G IO1 (**on/off only, no PWM**) |
| Touch reset | — | CH422G IO0 |

The backlight being binary rather than dimmable is a real UX regression against the
CYD boards, which have LEDC PWM. §11 carries it as a risk.

### 3.3 The USB / CAN Multiplex (critical constraint)

The board routes ESP32-S3 **GPIO 19 and GPIO 20** through an FSUSB42UMX analog
switch. On the ESP32-S3 those two pins *are* the native USB D− and D+ pair. The
switch therefore selects between:

| `USB_SEL` (CH422G IO4) | GPIO 19/20 connected to | Consequence |
| ----------------------- | ------------------------ | ----------- |
| LOW (power-on default)  | USB-C connector | USB CDC / serial works, **CAN dead** |
| HIGH                   | TJA1051 transceiver     | **CAN works, USB dead** |

There is no third state and no software workaround: while CAN is enabled the
device cannot be flashed over USB, cannot be monitored over USB, and cannot receive
a USB-serial reset.

This is why §7.5 exists, and it drives three design rules:

- **R1 — Boot must default to USB.** The CH422G powers up with IO4 low, so a board
  that is bricked can always be reflashed by power-cycling. The firmware must
  never leave `USB_SEL` high across a watchdog restart it did not intend.
- **R2 — OTA over WiFi is the only in-service update path.** ArduinoOTA (`:3232`)
  and the `/update` HTTP endpoint are the production path; `esp32usb` is a
  recovery tool used at the bench with CAN disabled.
- **R3 — `Serial` must be UART0, not `HWCDC`.** The template's serial logging has
  to survive `USB_SEL = HIGH`, which it does on the UART0 pins (43/44) but not on
  native USB. Verify which one the module initializes; if it is `HWCDC`, the
  template needs a board-conditional.

The recovery path is therefore: hold BOOT, power-cycle, `USB_SEL` comes up low,
flash over USB. To get back to CAN, reset — no reflash required.

**Debugging consequence.** This is not merely an inconvenience. On this board the
device loses its serial console at the exact moment it becomes interesting — the
moment CAN frames start arriving. Every diagnostic that matters therefore has to be
readable **on the panel itself**, which is why §7.6 and §8.12 exist and why N10
makes "no dependence on `Serial`" a hard rule rather than a nicety.

### 3.4 CH422G IO Expander

| Signal | CH422G IO | Meaning | Level that works |
| ------ | --------- | ------- | ---------------- |
| TP_RST | IO0 | GT911 reset | HIGH to release from reset |
| LCD_BL | IO1 | backlight enable | HIGH = lit |
| LCD_RST | IO2 | ST7262 reset | HIGH = released |
| SD_CS | IO3 | SD card chip select | **LOW** = card selected |
| USB_SEL | IO4 | USB ⇄ CAN mux | HIGH = CAN, LOW = USB |

Two properties make a naive "GPIO-like" abstraction wrong:

1. **The SD card CS is active-low on the expander**, so "SD enabled" is written as
   a *cleared* bit. Code that mirrors a GPIO mental model gets this inverted and
   the card is simply never seen.
2. **Several signals are latched by the expander's output-enable and mode
   registers.** The Waveshare examples write a mode byte to one register and a
   pattern byte to another. `tpl_waveshare_lcd7` will own that byte layout behind
   named setters (§6.5) rather than exporting raw register pokes, so no project can
   corrupt a neighbouring bit.

## 4. Requirements

### 4.1 Functional

| ID | Requirement |
| -- | ----------- |
| F1 | Display a home screen on the 7" panel in **portrait** orientation (480 × 800 logical). |
| F2 | Large clock, **HH:MM only**, no seconds, from NTP. |
| F3 | Stylized house-heater graphic showing three live temperatures: **chamber**, **water in**, **water out**. |
| F4 | Show current power consumption, sourced from CAN. |
| F5 | Three tappable zones (one per temperature) opening a detail screen each. |
| F6 | A gear icon in the bottom-right corner opening the CAN monitor feature. |
| F7 | Every detail screen has a working back button. |
| F8 | Every detail screen carries a **history chart** switchable over **1 day / 1 week / 1 month**. |
| F9 | Record history to the SD card on the board. |
| F10 | CAN decoding strictly via `haus_can.h`; no locally invented IDs. |
| F11 | OTA works through the normal template path. |
| F12 | The whole application runs on a developer Mac without flashing hardware. |
| F13 | When USB is unavailable (CAN enabled), diagnostics remain fully accessible **on the panel**: a 1 MB cyclic PSRAM log buffer and a scrollable log screen with follow-at-end. |
| F14 | A temperature's function (chamber / water-in / water-out) is resolved through **sensor ID**, not through CAN rank: `slot → rank → DS18B20 ROM → function`. Rank is a derived, shiftable address; the function belongs to the sensor and travels with it. |

### 4.2 Non-Functional

| ID | Requirement |
| -- | ----------- |
| N1 | **LVGL ≥ 9.2**, matching the version already used by `19_CYD-project` and `26_CYD_CAN_monitor`. 9.2 is the first release with a working `lv_display_set_rotation()`, which the RGB panel needs. |
| N2 | LVGL stays single-threaded (`LV_USE_OS = LV_OS_NONE`), driven from `loop()`, exactly as `26_CYD_CAN_monitor` does. |
| N3 | CAN reception never blocks the LVGL task. |
| N4 | SD writes never block the LVGL task. |
| N5 | House-state code (decoding, formatting, chart maths) is pure C++ with **no Arduino, ESP-IDF or LVGL dependencies**, so it compiles natively on macOS. |
| N6 | Memory budget: **heap** allocations stay under 64 KB. The two deliberate exceptions are *static* PSRAM buffers, which are sized at compile time and never reallocated — the decoded background image (768 KB, §8.3) and the 6 KB chart chunk buffer. The LVGL draw buffer stays in **internal** RAM, because the panel's DMA cannot cross the octal-PSRAM boundary. |
| N7 | The device stays reachable: WiFi/OTA/watchdog behave as in every other project. |
| N8 | Fonts and background images live in PSRAM or flash, not internal RAM. |
| N9 | Rebuilding `platformio.ini` for a board switch requires changing exactly one `-D` flag. |
| N10 | **No code path may depend on a working `Serial`.** With CAN enabled USB is gone, so every diagnostic must reach the PSRAM ring, and from there one of the three sinks: the WebSocket (`ws://<host>:81`), the log screen, or the SD dump. The ring must never block on a dead UART/CDC handle or on a stalled socket. |
| N11 | Diagnostic logging is always compiled in. The temptation to strip it for "release" is exactly backwards on a board whose only debugging surface is its own display and its own WiFi. |
| N12 | Log volume is bounded at every stage: capped write rate, a 750 ms WS drain with an 8 KB/tick ceiling and an explicit `dropped` marker, and a 5 Hz on-screen rebuild. A log storm must never become a way to make the panel unresponsive. |

### 4.3 The macOS Test Harness (F12)

This is a hard requirement, not a convenience, because the iteration loop
otherwise is *flash, mount, poke, watch serial* — minutes per UI change.

**Architecture.** The application is split so that everything interesting is
host-compilable:

```
src/
  app/        ← pure C++: model, decoding, formatting, chart maths. No deps.
  ui/         ← pure LVGL: widget construction, drawing, navigation. No deps.
  services/   ← thin interfaces (CAN feed, recorder) declared in app/, implemented twice
  esp32/      ← ESP32-only: TWAI task, SD backend, LVGL display/touch driver
  host/       ← macOS-only: SDL driver, CAN replay stub, in-memory SD stub
```

The single build-flag switch `HAUS_HOST` selects the second implementation of each
service interface. Nothing above `services/` knows which one it got.

**Host stubs required.**

| Stub | Behaviour |
| ---- | --------- |
| `can_feed` | Replays a recorded frame log through the *production* `haus_can.h` decoder. Source: a captured `.jsonl`/`.csv` trace, or a hand-written fixture of the four heater IDs. Guarantees the host is exercising the same code the device runs. |
| `recorder` (SD) | An in-memory (or temp-file) backing store with the identical fixed-size-record API. Charts read through this API, so the host exercises the real read path, seek maths and downsampling. |
| `clock` | A settable virtual clock, so "one month of history" can be tested in a second and screenshots are deterministic. |
| Display / touch | LVGL's SDL port (`LV_USE_SDL`), giving a real 480 × 800 window with mouse injection. |

**Tooling.** `host/CMakeLists.txt` following LVGL's official `lv_port_pc`
layout, plus a `make run` target. CI (or a pre-commit hook) runs a headless
smoke test that boots the app, drives it through each screen via synthetic taps,
and fails on a screenshot diff or an assertion failure.

**Explicit non-goal.** The harness is *not* an emulator of the panel. It does not
model RGB timing, the CH422G, or touch calibration. Those stay verified on
hardware. What it guarantees is that layout, navigation, decoding, chart maths and
state transitions are correct without a flash — which is where UI bugs live.

## 5. Architecture Overview

```
                    ┌──────────────────────── Core 0 ────────────────────────┐
  CAN bus (125k)    │                                                      │
   ═══════════════► │  can_task (TWAI RX)      wifi_manager   net_watchdog │
                    │        │                                             │
                    │        ▼                                             │
                    │   haus_state  (single writer, double reader)         │
                    │   ───────────────────────────────────────            │
                    │        │  snapshots                                │
                    │        ▼                                             │
                    │   recorder_task ──► SD card (fixed-size records)     │
                    └────────│─────────────────────────────────────────────┘
                             │ volatile snapshot
                    ┌────────▼─────────── Core 1 / loop() ─────────────────┐
                    │  lv_task_handler()                                  │
                    │    └── ui::render(snapshot)                         │
                    │          ├── HomeScreen   (clock, heater, power)     │
                    │          ├── DetailChamber│DetailWaterIn│WaterOut   │
                    │          │     └── HistoryChart (1d / 1w / 1m)       │
                    │          └── CanScreen     (lifted from project 26)  │
                    │  panel: esp_lcd RGB  │  touch: GT911 I2C            │
                    └─────────────────────────────────────────────────────┘
```

Three decisions worth naming up front:

- **A single owner of house state.** `haus_state` is written only by the CAN task
  and read by the UI. This replaces `26_CYD_CAN_monitor`'s arrangement, where
  decoder side-effects (counter increments, SML reassembly) ran inside the 500 ms
  *display* loop and therefore in table order rather than bus order.
- **Recording is decoupled from display.** The recorder samples its own state on a
  fixed cadence regardless of whether anyone is looking, so a missing or slow UI
  can never create a gap in the history.
- **The UI reads snapshots, never the CAN task's state.** That is what makes the
  host harness possible and keeps `loop()` bounded.
- **One log, three sinks.** Diagnostics go to a 1 MB PSRAM ring that nothing else
  writes to. `Serial`, the WebSocket and the log screen are all derived views of
  it (§7.6). This is the only structure that survives USB disappearing the moment
  CAN starts working.

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
lv_display_t *tpl_ws7_display(uint8_t rotation,
                              void *buf, size_t buf_size);
lv_indev_t   *tpl_ws7_touch(void);                   // created with read_cb installed

// ---- geometry ------------------------------------------------------------
#define TPL_WS7_SCREEN_WIDTH   800    // native panel
#define TPL_WS7_SCREEN_HEIGHT  480
#define TPL_WS7_WIDTH          /* rotated logical width  */
#define TPL_WS7_HEIGHT         /* rotated logical height */

// ---- expander-backed signals (named; no raw register access) ---------------
esp_err_t tpl_ws7_backlight(bool on);                // CH422G IO1, binary only
esp_err_t tpl_ws7_touch_reset_assert(void);
esp_err_t tpl_ws7_touch_reset_release(void);
esp_err_t tpl_ws7_panel_reset(void);                // CH422G IO2

// ---- SD card chip select (active LOW on the expander) --------------------
esp_err_t tpl_ws7_sd_select(bool select);            // select == true drives IO3 low

// ---- CAN / USB mux -------------------------------------------------------
esp_err_t tpl_ws7_set_usb_mode(bool usb);           // IO4; usb=true -> CAN disabled
bool     tpl_ws7_can_available(void);                // IO4 high?
esp_err_t tpl_ws7_gpio19_20_mode(bool as_usb);      // USB-SERIAL-JTAG vs IOMUX GPIO
```

The last function exists because of a detail that is easy to miss: once `IO4` goes
high, pins 19/20 must be **IOMUX GPIOs for the CAN peripheral**, not USB. If the
LCD/CAN task installs a TWAI driver without re-muxing, the transceiver is fed
from a USB PHY and the bus is silent. `tpl_ws7_gpio19_20_mode()` makes the
transition explicit and idempotent rather than an ordering landmine.

`rotation` is accepted by both `setup()` and `display()` because the logical size
depends on it, and a caller that forgets to pass the same value twice gets a
compile-time-visible mismatch rather than a mirrored touch layer.

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
  `hres × N`-pixel strips is the memory-efficient choice. For 800 × 20 px at
  RGB565 that is ~32 KB per buffer in internal RAM — it must *not* be in PSRAM,
  because the DMA cannot cross the PSRAM boundary on an octal PSRAM part.
  The buffer size is a `-D` flag (`TPL_WS7_DRAW_LINES`, default 20) so the S3's
  512 KB of internal SRAM is spent deliberately. This is the opposite of the CYD
  boards, where a tenth-of-a-screen PSRAM buffer is fine.
- **Timing** is fixed by the panel: 16 MHz pixel clock, 4/8/8 horizontal and
  4/8/8 vertical porches, `hsync_idle_low`/`vsync_idle_low` off (active high),
  `de_idle_high` off, pixel clock idle high. These go in as constants in the
  header rather than magic numbers at the call site.
- **`RGB_BOUNCE_LINES = 10`** and `RGB_TRAINING_LINES = 16` are kept from the
  Waveshare reference; the latter matters because the first lines after a reset
  are not valid on this panel.

### 6.4 Touch pipeline (GT911)

```cpp
esp_lcd_panel_io_i2c_config_t io_cfg = {
    .dev_addr = 0x5D, .control_panel_io = backlight_io /* shares the bus */,
    .on_color_trans_done = NULL, .flags = { .disable_control_panel = false },
};
esp_lcd_touch_panel_io_i2c_config_t tp_io_cfg = {
    .dev_addr = 0x5D, .control_panel_io = backlight_io,
};
esp_lcd_touch_gt911_config_t tp_cfg = {
    .i2c_port = 0, .rst_gpio_num = -1,           // reset is via CH422G, not a GPIO
    .int_gpio_num = -1,                         // no INT line broken out either
};
```

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

The expander is a shared, mutable byte. The module therefore:

1. Initialises it once in `tpl_ws7_setup()`: set output mode, then establish a
   known base pattern.
2. Caches the current pattern in a module-local variable.
3. Exposes only `read-modify-write` setters, each of which recomputes the pattern
   and pushes the full byte. No project ever writes the raw register.
4. Serialises every access with a short mutex, because touch polling, the SD
   backend and the CAN task all touch it.

The base pattern is derived from the Waveshare examples so that the bits not named
in §3.4 keep whatever value the reference firmware gives them. The module's
invariant to state in a comment: **bit 3 (USB_SEL) is the only one any project may
ever want to change after bring-up, and the CAN path must always be able to put it
back.**

### 6.6 Portrait orientation

Portrait is the only supported orientation for this project, but the module
supports 0–3 because it costs one macro and keeps the abstraction honest.

| rotation | logical size | panel | note |
| -------- | ------------ | ----- | ---- |
| 0 | 480 × 800 | 800 × 480 native | the portrait case |
| 1 | 800 × 480 | 800 × 480 native | landscape, no transform |
| 2 | 480 × 800 | 800 × 480 | portrait, 180° |
| 3 | 800 × 480 | 800 × 480 | landscape, mirrored |

`TPL_WS7_WIDTH`/`TPL_WS7_HEIGHT` are defined from `TPL_WS7_ROTATION` so that UI
code writes `TPL_WS7_WIDTH` and gets the truth.

### 6.7 Integration points in `template.h`, `tpl_system.h`, `tpl_sd`

**`template.h`** — add `#include "tpl_waveshare_lcd7.h"`. The header is guarded
internally by `#if defined(IS_WAVESHARE_LCD7)` so non-Waveshare projects — which is
all of the existing ones — pull in nothing and link unchanged. Same discipline as
`tpl_display.h`.

**`tpl_system.h`** — add the LED pin block:

```cpp
#ifdef IS_WAVESHARE_LCD7
#define tpl_ledPin      4    // BOOT-adjacent status LED, if the board has one
#define tpl_backlightPin 2   // CH422G IO1 — logical, not a real GPIO
#endif
```

`tpl_backlightPin` is *logical* on this board: no ESP32 GPIO drives the backlight.
It exists only so `tpl_wifi_setup(..., ledPin)` and the CYD-style call sites keep
compiling. The comment must say so, or the next reader will try to `analogWrite()`
it.

**`tpl_sd`** — this needs a real change, not just a define. `tpl_sd` assumes a
directly-driven CS pin (`TPL_SD_CS`, default 5). Here the CS comes from the CH422G
and there is no GPIO at all. Options considered:

| Option | Verdict |
| ------ | ------- |
| A. Add a `TPL_SD_CS_VIA_EXPANDER` flag to `tpl_sd` | Rejected — pushes CH422G register knowledge into a generic module, and the SD backend still would not do recording. |
| B. Give `tpl_sd` a settable "pre-select" hook | Rejected — an untyped function pointer for a two-call sequence is worse than duplication. |
| **C. New `recorder` service (§7.4) with its own SD backend; leave `tpl_sd` alone** | **Chosen.** |

`tpl_sd` stays as the *browser* for files (`/sd/list`, `/sd/read`, `/sd/download`)
and the recorder owns *writing*. Both use the same physical card and cooperate by
never holding the card open across a flush — the recorder flushes and closes before
`loop()` may call a read endpoint, and reads are opened O_RDONLY.

**Symlinks.** `make links` regenerates `include/`, `src/`, `data/`, `tools/`
symlinks for every project; `tpl_waveshare_lcd7.{h,cpp}` are added to that sweep
automatically by the existing Makefile rule, so no per-project work is needed.

## 7. Deliverable B — Project `ESP32/28_hauszentrale`

### 7.1 Project skeleton and `platformio.ini`

Two environments, per the repo convention, plus a third for the host harness.

```ini
[common]
hostname = esp32_28
platform = espressif32 @ 6.13.0
board    = esp32-s3-devkitc-1
framework = arduino
board_build.f_cpu = 240000000L
board_build.partitions = custom_8m_ota.csv   ; 2x3 MB app + 512 KB SPIFFS
build_flags =
    -I include
    -D LV_LVGL_H_INCLUDE_SIMPLE
    -D LV_CONF_INCLUDE_SIMPLE
    -D CORE_DEBUG_LEVEL=1
    -D HOSTNAME=\"${common.hostname}\"
    -D NET_WATCHDOG=\"192.168.1.1\"
lib_deps =
    lvgl/lvgl@^9.2.0
    549@^2.2.1
    bblanchon/ArduinoJson@^7
extra_scripts = pre:../prj_template/tools/generate_version.py

[env:esp32ota]
build_flags = ${common.build_flags}
    -D IS_WAVESHARE_LCD7
    -D TPL_WS7_ROTATION=0          ; portrait: 480 x 800 logical
    -D TPL_WS7_DRAW_LINES=20
    -D HAUS_CAN_TX=19
    -D HAUS_CAN_RX=20
    -D HAUS_SD_ENABLE=1
upload_protocol = espota
upload_port = ${common.hostname}.local
upload_flags = --port=3232 --host_port=56789

[env:esp32usb]
build_flags = ${env:esp32ota.build_flags}
upload_protocol = esptool
upload_speed = 921600
; NOTE: only reachable while CH422G USB_SEL is LOW. See 3.3 / R1.
```

Points worth flagging:

- **`partitions`.** `min_spiffs.csv` (the repo default) leaves 192 KB of SPIFFS. A
  project shipping a background image and several fonts wants headroom. Proposed
  custom table: `nvs` 20 KB, `otadata` 8 KB, two 3 MB `app` slots, 512 KB SPIFFS.
  OTA is a stated requirement (F11), so the two-app-slot layout is non-negotiable;
  the sizes are the tunable part.
- **`lv_conf.h` must be this project's own.** The repo currently borrows
  `19_CYD-project/include/lv_conf.h` by symlink. That config was written for a
  240-320 px panel and lacks `LV_USE_IMAGE` PNG decoding and the wider Montserrat
  range the charts need (degree signs, arrows). Reusing it will silently cost
  features, so `28_hauszentrale/include/lv_conf.h` is a required new file.
- **`LV_DRAW_SW_ASYNC`** stays enabled. With an RGB panel the flush is the long
  pole; synchronous draw walks the whole object tree on the flush path.
- **`LV_DRAW_TRANSFORM_USE_MATRIX = 1`** is mandatory — `lv_display_set_rotation()`
  is a no-op without it, and rotation is the whole basis of the portrait mode
  (§6.4).
- **`-Werror`**, as in `26_CYD_CAN_monitor`, and the host env must build clean too.

### 7.2 CAN subsystem

```cpp
twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
    HAUS_CAN_TX, HAUS_CAN_RX, TWAI_MODE_NORMAL);
twai_timing_config_t  t = TWAI_TIMING_CONFIG_125KBITS();
twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
```

- **125 kbps**, matching every other node. `ACCEPT_ALL` is correct: the device must
  also see the `0x555` timestamp broadcast and the SML stream, which are not its own.
- **RX task, 4096 B, priority 3** — structurally `26_CYD_CAN_monitor`'s `CANTask`:
  drain the queue against a 100 ms alert timeout, re-arm `RX_DATA | ERR_PASS |
  BUS_ERROR | BUS_RECOVERED | RX_QUEUE_FULL`.
- **The alerts-config pointer must be non-NULL** (`uint32_t alerts_config = 0`).
  This already bit project 26; do not repeat it.
- **Errors are surfaced, not swallowed.** `ERR_PASS` / `BUS_ERROR` raise a bus-fault
  flag the home screen shows as a banner. A wall panel that silently stops updating
  is worse than one that says "CAN down".
- **TX:** one `0x555` frame per second carrying wall clock, plus a heartbeat on the
  device's own node ID — derived from the WiFi IP's last octet, exactly as project
  26 does. Listening therefore has a side benefit: the panel announces itself and
  its clock to the rest of the house, which the existing `TIME-BCAST` consumer
  already understands.

**Mux ordering.** CAN availability is not a compile-time constant:

```cpp
tpl_ws7_setup(TPL_WS7_ROTATION);
tpl_ws7_gpio19_20_mode(false);   // pins as IOMUX GPIOs, not USB-SERIAL-JTAG
tpl_ws7_set_usb_mode(false);     // IO4 HIGH -> CAN live, USB dead
/* ... twai_driver_install() / twai_start() ... */
```

Every step is idempotent and the order is fixed, because after `IO4` goes high there
is no USB fallback left with which to diagnose a mistake.

### 7.3 CAN monitor (lifted from `26_CYD_CAN_monitor`)

The gear icon opens the scope view that already exists in project 26. That code is
project-local and entangled: `protocol_detail.cpp` reads `g_can_id_table`,
`g_max_can_ids`, `g_sml[]` and `g_sml_count`, which `main.cpp` owns, and it depends
on `ui/screens.h` for `ROW_HEIGHT` and the CYD geometry macros.

Refactoring before reuse:

| Step | Action |
| ---- | ------ |
| 1 | Extract `can_id_entry_t`, `sml_reassembly_t`, the RX task and `can_table_update()` into `tpl_can_monitor.{h,cpp}` in `prj_template`. |
| 2 | Collapse `decode_protocol()` and `get_protocol_info()` into one `can_decode()` returning `{label, decoded, protocol_tag}`. Today the same switch over `haus_protocol_t` is written twice, with divergent labels. |
| 3 | Move SML reassembly out of the display path into the RX task, keyed by base ID. As written it reassembles in *table* order, not bus order — a latent correctness bug, not a cosmetic one. |
| 4 | Delete decoder side effects. Counters belong in the RX task, which is what "142 frames" was always supposed to mean. |
| 5 | Re-express row geometry in `TPL_WS7_WIDTH`/`TPL_WS7_HEIGHT` instead of `CYD_*`, so the monitor sizes itself for any panel. |

After step 5 the module is board-agnostic and project 26 can adopt it — which is
what makes this refactor worth doing twice over. **Project 26 must keep building
and passing afterwards**; it is the regression test for this step.

### 7.4 SD card data recording

`tpl_sd` is read-only by design (§6.7). Recording needs its own service, declared
in pure C++ so the host build can implement it too:

```cpp
// src/app/recorder.h
struct Sample {
  uint32_t epoch;        // minute-aligned unix time
  int16_t  chamber_c100; // 0.01 C
  int16_t  water_in_c100;
  int16_t  water_out_c100;
  uint16_t power_w;      // scaled: see 7.4 power source
  uint8_t  flags;        // VALID bits + FAULT bits
};
static_assert(sizeof(Sample) == 12, "fixed-size records enable seeking");

struct Recorder {
  virtual bool   open(const char *path) = 0;
  virtual bool   append(const Sample &s) = 0;
  virtual bool   flush() = 0;
  virtual size_t count() const = 0;
  virtual bool   read(size_t first, size_t count, Sample *out) = 0;  // random access
  virtual void   close() = 0;
};
```

- **ESP32 backend:** SD through the CH422G-controlled CS, append mode, `flush()` at
  least once a minute and always before any read.
- **Host backend:** identical semantics over a `std::vector` or temp file. This is
  what makes the chart path testable (§4.3).
- **Cadence:** one sample per minute, on the minute, from a dedicated task fed by
  `haus_state` snapshots — unaffected by UI stalls (N4).
- **Never block LVGL.** `flush()` is the only long card operation and runs on the
  recorder task. The card is not held open across a webserver read; reads are gated
  on the recorder having flushed and released (§6.7).
- **Fixed-size records are the key decision.** With a 12-byte record, reading the
  last 1440 samples is `seek(-1440*12, SEEK_END)` — no parsing, no scan, no index
  to maintain or corrupt. §9 depends on this.

**Power source for F4.** Two candidates are on the bus and the choice is not settled
(§12 Q1). The difference is isolated behind one `PowerSource` interface so the
choice is an enum in SPIFFS config, not a rewrite:

| Source | ID | Content | Assessment |
| ------ | -- | ------- | ---------- |
| SML smart meter | `0x600` / `0x700` | multi-frame SML stream from `AVR/07_stromzaehler` | The true household power. But `haus_can.h` today only *reassembles* SML — it has no semantic decoder. Needs either a small SML `1P.1.0` parser or a protocol addition. |
| Heater ADC window | `0x2C0` | max / min / peak-to-peak counts | Probably a heater current sensor. House *heater* power, cheap to use — but it is not "the house's power". |

Recommended default: SML primary, ADC window as a visible fallback, and an explicit
"—" when both are absent. A wall panel showing a stale kilowatt figure is a lie
with a fresh-looking timestamp.

### 7.5 OTA and firmware-update paths

Because `USB_SEL = HIGH` kills USB (§3.3), this section exists because of that one
fact.

| Path | Mechanism | When used |
| ---- | --------- | --------- |
| **ArduinoOTA** (primary) | `:3232` over mDNS via `tpl_wifi_setup` | Production. Unaffected by CAN. |
| **HTTP `/update`** | `tpl_webserver` POST handler | Alternate production path; also unaffected. |
| **USB flash** (recovery) | `esp32usb` env, `esptool` | Bench only, with CAN disabled. |

Requirements that follow:

- **`Serial` must survive.** Confirm the log port is UART0 and not `HWCDC`. If the
  core defaults to `HWCDC` on this S3 variant, `tpl_wifi_setup`'s OTA progress output
  and every `WATCH()` breadcrumb go dark in exactly the configuration where debugging
  matters most (§12 Q2).
- **Watchdog restarts must be safe.** `tpl_net_watchdog` restarting the device while
  CAN is enabled brings it back with `IO4` low (the CH422G powers up that way): CAN
  stays alive and USB returns. That is the recovery path and it works. The bad case
  is a crash loop *after* `IO4` goes high again — recoverable by power cycle,
  reflash only if it persists. Crashes must therefore be logged somewhere that
  survives, which is the SD log from §7.4.
- **Boot-time settle.** Leave `USB_SEL` low for the first ~200 ms after boot. The
  Waveshare examples flip it immediately; a short settle reduces the class of
  "worked over USB, dead over CAN" surprises during bring-up.

### 7.6 Diagnostics — PSRAM log ring and on-panel access

`prj_template` already has `tpl_debug_log` (opt-in via `USE_TPL_DEBUG_LOG`). It is
the right home and the wrong implementation for this board:

| Aspect | Current `tpl_debug_log` | Needed here |
| ------ | ----------------------- | ----------- |
| Storage | `static char[100][160]` — **internal** RAM, 16 KB | 1 MB in **PSRAM** |
| Capture | `esp_log_set_vprintf` only | also project/app-level logging |
| Access | HTTP `/log` + `/log/clear` only | **also** a scrollable on-screen view |
| Serial sink | unconditional `Serial.print(tmp)` | must be **detachable** — a dead USB handle can block |
| Sinks | `Serial` only | `Serial` while alive, **WebSocket** while a client is attached, **the panel** (§8.12) |
| Structure | fixed-length lines | variable-length, newline-delimited byte ring |
| Overflow | silently overwrites | reports how much was lost |

Since it is a template module, the change lands in `prj_template` and benefits any
project on a board without a console. It is opt-in, so nothing else changes.

**Storage.** A **1 MB PSRAM byte ring** (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`),
allocated once at `tpl_debug_log_setup()` and — importantly — **not** freed. Config:

```ini
-D USE_TPL_DEBUG_LOG
-D DEBUG_LOG_BYTES=1048576        ; 1 MB, PSRAM
-D DEBUG_LOG_SERIAL=1             ; mirror to Serial while it is alive
-D DEBUG_LOG_WS=1                 ; mirror to WebSocket clients on :81
-D TPL_WS7_LOG_SCREEN=1           ; enable the on-panel log view
```

**A byte ring, not fixed lines.** Messages have wildly different lengths; a
160-byte-capped ring wastes space on a 1 MB budget and truncates exactly the
multi-line ESP-IDF warnings that are worth reading. Records are
newline-terminated byte runs. Scrolling is implemented by scanning for newlines
from the ring start — a 1 MB scan is a few milliseconds, done only on scroll, not
per frame.

**Two-part prefix.** Each record is written as
`[<uptime_ms>][<level>][<tag>] <message>\n`, so the log is self-describing after
the fact and a message that arrives without a newline (a mid-line crash) is still
recovered up to its last newline.

**Three sinks, ring authoritative.**

| Sink | Availability | Behaviour |
| ---- | ------------ | --------- |
| **PSRAM ring** | always | The single copy. Everything else is derived from it. |
| `Serial` | only while `USB_SEL` is LOW | Mirrored at write time. |
| **WebSocket** `:81` | only while WiFi is up **and** a client is attached | Streamed, `tail -f` style. |
| **Panel** (§8.12) | always | Windowed render of the ring. |

The WebSocket sink is what makes this project debuggable from the desk. The panel
is a good *last-resort* console — readable, glanceable, survives everything — but
reading a stack trace by squinting at a 16 px monospace font on a wall panel is
not how one debugs. With `ws://esp32_28.local:81` open on the Mac, `tail -f` on the
socket gives a real console, and the CAN/USB conflict stops mattering.

**Serial sink is detachable.** `tpl_log_set_serial(bool)` is called with `false`
immediately after `tpl_ws7_set_usb_mode(false)` in §7.2's sequence. This is not
cosmetic: on ESP32, writing to a USB CDC endpoint that is not enumerated can block
the task that writes, which would turn "logging is unavailable" into "logging
hangs the system". The ring write never blocks — it copies under a short mutex
and drops the record if the mutex cannot be taken within 5 ms.

**The WebSocket sink is a pull, not a push.** Writing to a socket from the logging
path would couple logging to a client's TCP window — a slow or paused client would
stall whatever task happened to log. Instead the ring is the buffer and the
already-running `TaskWebSocketCore0` drains it:

```
TaskWebSocketCore0 (core 0, existing, 750 ms tick)
  └─ every tick, if webSocket.connectedClients(...):
        cursor = ws_cursor            // per-connection, initialised to "oldest"
        send records (cursor, head] as JSON, capped at 8 KB per tick
        ws_cursor = head
```

- Cost with **no client attached is zero** — one `connectedClients()` call.
- A client joining gets a backlog: its cursor starts at `max(head - 8 KB, oldest)`,
  so it sees recent context rather than an empty stream.
- The 8 KB/tick cap bounds bandwidth and per-tick work; if the log outruns it, the
  cursor jumps forward and the client sees a `{dropped: N}` marker rather than a
  silent discontinuity. Same honesty rule as the chart's gap markers (§9.3).
- Records go out as JSON — `{"t":1234567,"l":"E","tag":"twai","msg":"bus error"}` —
  which is trivially greppable *and* trivially parseable. The prefix from the ring
  stays in the payload for the text mirror.
- `webSocket` is already a global exported by `tpl_websocket.h`, so no template
  change is needed to reach `broadcastTXT` / `sendTXT`.
- **Unauthenticated.** `tpl_websocket` serves the LAN with no auth today, and log
  content is house telemetry (temperatures, heater behaviour, firmware versions).
  That is acceptable on a home LAN but it does widen the fingerprinting surface.
  §12 Q10.

**What gets captured.** `esp_log_set_vprintf` covers WiFi, lwIP, `esp_lcd` and the
rest of ESP-IDF, which is the bulk of it. It does **not** capture
`Serial.println()`, so the template's direct serial output would bypass the ring
entirely. Two ways to close that gap:

| Option | Approach | Verdict |
| ------ | -------- | ------- |
| A | Convert the template's `Serial.println` calls to `TPL_LOGx` macros | Correct but touches every template module and every project |
| B | If `Serial` is UART0, additionally hook `uart_set_vprintf()` so the port's own output is captured too | **Chosen** — one hook, catches everything, and it depends on §12 Q2 |
| C | Accept the gap and add `TPL_LOGx` only in project code | Fallback if Q2 shows `Serial` is `HWCDC` |

Under option B the ring is the single sink and `Serial` becomes a mirror of it,
which is the correct topology: **one log, three destinations, ring authoritative.**
Option B and the WebSocket sink are complementary, not alternatives — B makes the
ring capture *everything*, the socket makes that capture *readable from a desk*,
and the panel makes it readable when the desk is not there.

**Preservation on crash.** Because the ring is in PSRAM it survives a soft reset but
not a power cut. On `tpl_command = CmdSaveConfig`, on a `WATCH()` breadcrumb, and
on every `tpl_hard_restart()` path, the ring is flushed to `/log_YYYYMMDD_hhmmss.txt`
on the SD card before the reset proceeds. A wall panel that reboots mysteriously
should leave behind a file saying why.

**Level filtering.** `DEBUG_LOG_LEVEL` is build-configurable, default `INFO` for IDF
internals. The on-screen view adds a level filter row (E / W / I / D) so a noisy
subsystem can be muted on the device without a rebuild — which, with no serial,
would otherwise mean a rebuild for every debugging session.

### 7.7 Sensor resolution — rank is an address, the function belongs to the sensor

`AVR/01_heizung_temp`'s README documents the current installation:

| Rank | Role (verbatim) | ROM (sensor ID) | CAN ID |
| ---- | --------------- | --------------- | ------ |
| 0 | Heater (on the heating element) | `281BF75704E13C26` | `0x180` |
| 1 | To the heater (supply side) | `2876C25704E13CF9` | `0x181` |
| 2 | Board temperature (on the rf-nano PCB) | `28B9EE81E3E13C2E` | `0x182` |

That table is a **snapshot of one installation**, not a protocol guarantee. The
actual chain is:

```
  function        the physical thing a sensor measures
     |            (chamber / water in / water out / board)  -- a property of the
     |                                                          SENSOR, travels
     |                                                          with it
     v
  sensor ID       the DS18B20 ROM. Unique, permanent, travels with the probe.
     |            0x180 + rank announces it, "burst at boot, then 60 s"
     v
  rank            POSITION in the sorted list of *currently present* sensors.
     |            Derived, transient. Shifts the moment one is unplugged.
     v
  slot            index into the 0x200 frame's four int16_t.
```

So the resolution the panel performs on every update is **four hops, not one**:

```cpp
// slot r of 0x200  ->  rank r  ->  sensor ID  ->  function
SlotRole role = roles.by_sensor_id(rank_rom[r]);
```

**The consequence that matters.** The rank column in the table above is incidental.
If the water-in probe is pulled:

- the two remaining sensors re-sort, and their ranks shift;
- **the functions travel with their sensors**, so the panel still labels the right
  number "water in" — provided it resolved through `ROM → function`;
- if the panel had cached "rank 1 = water in", it would now be labelling whatever
  sensor sorted into rank 1, which is the rf-nano board probe, as the water supply
  temperature. A plausible number under a false label.

That is the failure mode F14 exists to prevent, and it is why the README insists the
receiver "detects and reports this rather than silently substituting a different
sensor" — the panel must honour that contract rather than work around it.

**Two rules.**

1. **Rank → ROM is learned, never assumed.** It is refreshed from every `0x180 + r`
   identity frame and expires after a timeout (say 90 s = three missed broadcasts).
   An expired identity demotes the slot to *unknown* rather than leaving a stale
   binding in place.
2. **ROM → function is local, configured, and editable.** It is not on the bus. The
   table lives in SPIFFS (`sensor_roles[]`), seeded from the ROMs above, and is
   editable over HTTP and from the log screen — because a *different* installation
   of this panel will have different sensors, and hardcoding ROMs in the firmware
   would make the project work on exactly one machine.

**Display rules.**

| State | Home screen | Status strip |
| ----- | ----------- | ------------ |
| Slot with a bound function, identity seen | Value in the correct zone | — |
| Slot with a bound function, identity expired or never seen | `—` | "sensors" hint |
| Slot identity seen but ROM has no local function binding | `—`, zone greyed | "unbound sensor" |
| ROM bound to a function appears at a *different* rank than expected | Correct value, correct label | "rank shift" logged at `!` |

The last row is the honest outcome of a rank shift: nothing is wrong with the
reading, so the panel shows it — but it says so, because an electrician who has
just swapped two probes deserves to know the addressing moved.

**What is missing.** The current installation has no sensor on the heater's
*return* line, so there is no water-out function to bind (R16, Q9). Until one is
fitted, slot 2's function is `board`, and the third home-screen zone has no source
and renders `—`. If Q9 resolves to fitting a fourth DS18B20, the only change
needed here is one more entry in `sensor_roles[]`; nothing in the display or chart
code moves.

## 8. GUI Design — Portrait

### 8.1 Design language

A wall panel is not a settings dialog. Three rules, in priority order:

1. **Readable from across a room, in one glance.** The clock and the three
   temperatures are sized for 2-3 m viewing distance. Everything else is secondary
   and may require approaching the panel.
2. **The picture carries the data.** A stylised heater with glowing flow pipes
   communicates "water is moving at 62 °C" faster than a table of numbers, and the
   picture doubles as the touch target. There is no legend to read.
3. **Restraint.** A dark, low-contrast background image; a warm accent for heat;
   exactly one saturated colour used to mean "attention". No gradients on text, no
   drop shadows on text (they disappear at this size), no animation that does not
   carry information.

The only animation on the home screen is the flame, whose brightness tracks chamber
temperature — a slow 6 s breathing cycle modulated by the live value. That is a
deliberate exception to "no gratuitous motion": it is data, it is the only thing
that moves, and it makes the panel feel alive when the house is quiet.

### 8.2 Home screen layout

480 × 800 logical pixels (portrait). Bands, top to bottom:

```
 y=0
 +---------------------------------------------------+  0
 |  Mon 14 Oct                        [CAN ok] [SD] |   status strip, 28 px
 +---------------------------------------------------+  28
 |                                                   |
 |                   08:47                           |   clock, ~150 px tall
 |                                                   |   Montserrat ~110 px
 +---------------------------------------------------+  190
 |                                                   |
 |        +----------------------+                   |
 |        |                      |  <- flame          |
 |  21.4  |      CHAMBER         |     ~120 px       |
 |   degC |                      |                   |
 |        +-----------+----------+                   |
 |    62.1 degC   ^   48.6 degC                      |   pipe labels
 |     water in     water out                        |
 |                                                   |
 +---------------------------------------------------+  620
 |                                                   |
 |   CURRENT POWER                                  |
 |   1.284 kW                                       |   ~70 px value
 |                                                   |
 +---------------------------------------------------+  740
 |  esp32_28 .local           uptime 6d 04h   [gear] |   footer, 60 px
 +---------------------------------------------------+  800
```

Notes:

- **Clock without seconds** (F2), because the panel is glanced at, not watched.
  The seconds are known to the renderer but not shown; an optional seconds overlay
  exists behind a long-press for debugging, off by default.
- **Date and weekday** share the status strip. Without them "08:47" is ambiguous.
- **Temperatures sit on the graphic**, not in a row beneath it. A number with a
  degree sign next to a pipe reads as a property *of that pipe*, which is the point
  of the stylised rendering.
- **The gear is bottom-right** (F6), 64 × 64 px, in the footer — deliberately the
  least prominent element on the screen, reachable but not inviting. It is the only
  rounded-rect button in the footer so that it reads as an affordance.
- **Status strip indicators** are small dots, not text: green CAN, amber CAN fault,
  green SD, red SD fault, grey WiFi-off. Text would consume the band that the clock
  needs.

### 8.3 Background image pipeline

The background is a 480 × 800 PNG — a dim, abstract interior: a dark wall with a
faint warm gradient low and centre where the heater graphic will sit.

Pipeline:

| Stage | Detail |
| ----- | ------ |
| Store | `data/bg.png` in **SPIFFS**, uploaded with `pio run -t uploadfs`. ~120 KB as a 16-bit PNG. Must not live in the app partition: 480×800 RGB565 decoded is 768 KB, and the app slot is a shared OTA budget. |
| Decode | LVGL's built-in PNG decoder (`LV_USE_PNG`) at boot, **once**, into a dedicated PSRAM buffer declared statically, not from the heap (N6). |
| Cache | The decoded buffer is wrapped in an `lv_image_dsc_t` with `LV_COLOR_FORMAT_RGB565` and installed as the screen's background `lv_image`. |
| Release | The decoded-in-RAM PNG copy is freed; the SPIFFS copy stays for the next boot. |
| Failure | If SPIFFS is empty or decoding fails, fall back to a flat dark background. The panel must never boot to a black rectangle — a wall panel showing nothing is indistinguishable from a broken one. |

Layering is: `lv_layer_bottom()` holds the static image; the flame, pipes, labels,
clock and buttons are ordinary widgets on the default layer. Redrawing the
background therefore costs nothing per frame — only the animated flame region is
invalidated.

### 8.4 The house-heater graphic

Drawn as **vector widgets**, not a bitmap. The reason is that the graphic must
react: the flame brightens, the pipe colours shift with temperature, and a stuck
sensor has to look stuck rather than frozen in a pretty picture.

Construction, using `lv_canvas` for the drawing and `lv_obj_t` for the labels:

| Element | Technique | Driven by |
| ------- | --------- | --------- |
| Cabinet outline | `draw_rect` with radius, 2 px stroke, low-alpha fill | static |
| Firebox | `draw_rect` rounded + inner radial warmth | chamber temperature |
| Flame | 3 stacked `lv_arc` shapes at varying widths, opacity and y-offset | chamber temperature; 6 s breathing |
| Flow pipes in/out | Rounded-cap `draw_line` per segment, colour lerped along a blue→amber→red ramp | water-in / water-out temperature |
| Flow animation | 6 bright dashes marching along each pipe, 1.4 s loop, direction follows flow | flow presence (PWM duty > 0) |
| Value labels | `lv_label`, Montserrat 28, with a semi-transparent plate behind | live value |
| Degree/unit | part of the label string, smaller via a second label | — |

**Colour ramps.** Temperature → colour uses a single shared ramp so all three
temperatures are visually comparable: below 30 °C a desaturated blue, 30-60 °C
amber, above 60 °C red, with a hard clamp so a failed sensor reading `-327.68` (the
`HAUS_CAN_TEMP_EMPTY` sentinel) renders grey rather than as an extreme. **This
matters:** `haus_can.h` uses `INT16_MIN` to mean "no sensor in this slot", and a
naive cast to float would put a huge negative number on screen as a legitimate
temperature. Empty and invalid must be visually distinct from cold.

**Texture for "fancy".** Three cheap additions that survive an RGB flush budget:
a soft outer glow on the firebox (drawn as three concentric low-alpha rounded rects),
a per-pixel dithered noise overlay in the dark areas (a 4 × 4 tiled 16×16 ARGB
pattern at ~6 % opacity, one `lv_image` tile), and a 1 px inner highlight along the
top edge of the cabinet. Together these read as a rendered scene rather than flat
shapes, and together they cost three cached images, not three per-frame passes.

### 8.5 Touch model: three zones + gear

Zones are **elliptical/rectangular hit regions declared as invisible `lv_obj_t`
button overlays**, not ad-hoc coordinate tests in a read callback. That keeps hit
areas independent of the drawing and gives LVGL's own press/click handling, focus
and scroll-guard behaviour for free.

| Zone | Region (portrait coords) | Opens |
| ---- | ------------------------ | ----- |
| Chamber | centre rect over the firebox, ~300 × 180 at y≈250 | Detail screen 1 |
| Water in | pipe segment + label, ~180 × 120 at y≈500 | Detail screen 2 |
| Water out | pipe segment + label, ~180 × 120 at y≈500 | Detail screen 3 |
| Gear | 64 × 64 at (416, 736) | CAN monitor |
| Clock | (optional) whole clock area | toggles °C / °F and a 24 h clock |

**Feedback.** Every zone shows a `PRESSED` state: the overlay tints 12 % white and
the label scales to 1.04. No ripple animation — at this size it is invisible and it
costs redraws.

**Zones never overlap**, so no hit-test arbitration is needed. Where the water-in
and water-out labels sit close together, the hit rectangles are separated by 8 px
of dead zone — an intentional guard against a mis-tap between two adjacent targets.

**Whole-screen gestures** (secondary, all off the critical path):

| Gesture | Effect |
| ------- | ------ |
| Long-press clock, 1.5 s | toggle 12/24 h and °C/°F |
| **Long-press status strip, 1.5 s** | **open the log screen** (§8.12) |
| Three-finger tap | back to home from any screen |

Colour inversion moved off the status strip, because the log screen has to live
there: when USB is gone and the network is suspect, the log is the *first* thing
one reaches for, and burying it behind the gear would be wrong. Inversion is now a
toolbar button on the log screen, which is also a more sensible place for a
colour-mode switch.

Long-press uses `TOUCH_DEBOUNCE_MS = 40` on the raw read, the same debounce
`26_CYD_CAN_monitor` needed for the XPT2046 — even though the GT911 is capacitive,
its I2C reports still need de-glitching, and a spurious long-press that opens a
screen is exactly the kind of bug that makes a wall panel untrustworthy.

### 8.6 Detail screen 1 — Chamber

Shared skeleton for all four detail screens (§8.11 defines it):

- **Header**: title, large live value, unit, and a **range selector** for the
  history chart (1 day / 1 week / 1 month) as a 3-way segmented control.
- **Chart**: 440 × 260 px at x=20, y=200.
- **Readouts** below the chart: min / max / mean / now over the selected range, plus
  a "data age" line that says `12 min ago` if the newest sample is stale.
- **Back button**, 48 px arrow, top-left.

Chamber-specific content:

- Target temperature with a horizontal reference line on the chart (dashed).
- Current PWM duty as a thin bar under the header — the actuator's own view of the
  house, and the explanation for why the chamber temperature is not moving.
- Time above target, accumulated over the selected range. This is the number that
  actually tells you whether the heater is efficient, and it cannot be derived from
  a snapshot.

### 8.7 Detail screen 2 — Water in

Same skeleton. Chamber-specific additions replaced by:

- Delta to the chamber temperature, which is what actually tells you how hard the
  heat exchanger is working.
- A stacked min/mean/max envelope is not used here; flow-in temperature is stable
  enough for a plain mean line with a min/max band drawn at 20 % alpha.

### 8.8 Detail screen 3 — Water out

Same skeleton. Additions:

- **Rise**: water-out minus water-in, which is the efficiency metric.
- The same chart can overlay water-in as a faint second line for direct comparison.
  This is the one place a second series earns its keep, and it is why the range
  selector is shared between the two screens rather than each having its own state.
- **This screen currently has no data source.** There is no water-out sensor on the
  bus — the third probe measures the transmitter board's PCB, not water (R16, Q9).
  Until a sensor is fitted, the zone and this screen render as `—` with the reason
  shown, not as rank 2's value. If Q9 resolves to option (a), a fourth DS18B20 on
  the return line, the only change needed is a ROM binding (§7.7).

### 8.9 Detail screen 4 — Power

Reached by tapping the power readout rather than one of the three temperature zones
(F5 lists three zones; the power area is a fourth, documented affordance).

Same skeleton. Additions:

- Unit toggle kW / W auto-selected by magnitude, with a manual override in the
  header.
- Source badge: `SML`, `ADC`, or `—` so the number's provenance is never ambiguous
  (§7.4). When the source is the ADC fallback, the badge is amber.
- A daily-bar strip beneath the chart, 31 bars for 1 month / 7 for 1 week / hourly
  bins for 1 day. Power is the one series where a sum is more legible than a
  trace — nobody wants to integrate a curve by eye.

### 8.10 CAN monitor screen (gear)

The lifted scope from §7.3, restyled:

- Row height 44 px instead of 18, Montserrat 20, 480 px wide — the row count on a
  7" portrait screen becomes `(800 - header - footer) / 44 ≈ 16` rows, which is
  fewer than project 26's 23 on a 3.5" landscape but far more legible.
- Tapping a row opens the existing detail overlay, moved to a full-screen
  `lv_obj_t` on `lv_layer_top()` rather than a separate LVGL screen, preserving the
  approach that already works in project 26.
- The monitor's own header carries the bus-error banner from §7.2 in place of the
  home screen's status dot.
- A **range selector is not shown** — the monitor is live-only by design. History
  lives in the four detail screens; mixing the two would double the reading model
  for no benefit.

### 8.11 Navigation and back button

One screen manager, deliberately small:

```cpp
namespace ui {
enum class Screen { Home, Chamber, WaterIn, WaterOut, Power, Can, Log };
void push(Screen s);     // records the stack, loads
void pop();              // -> previous, or Home at depth 1
Screen current();
}
```

- **A real stack, not a flag.** The water-out screen can be reached from home *and*
  from the water-in screen's comparison link; a single `current_screen` variable
  would make "back" ambiguous.
- **Back button** is a 48 px `LV_SYMBOL_LEFT` button, top-left, 8 px inset,
  present on every screen except home. Implemented as an LVGL button, not a
  coordinate test — a scrollable container swallows a tap as a scroll on
  resistive touch, and this avoids repeating that class of bug.
- **Transitions** are an instant swap plus a 120 ms cross-fade on the content layer
  only. The background image does not fade, so the screen never flashes black.
- **Idle return**: after 3 minutes with no touch, return to home. A wall panel left
  on a detail screen is a wall panel showing a stale number to whoever walks past.
  The log screen is exempt while follow-at-end is on — an unattended log view
  scrolling itself is the expected behaviour, not a stale display.

### 8.12 Log screen

Reached by long-pressing the home screen's status strip (§8.5). This is the
project's diagnostic surface, and it has to work with no network, no serial and no
debugger — the three things that are gone exactly when it is needed.

```
 y=0
 +---------------------------------------------------+  0
 | [< Back]   LOG        E W I D   [INV] [CLR] [SAVE] |  toolbar, 44 px
 +---------------------------------------------------+  44
 | 1234.567 E esp_lcd: panel_io: timeout on data     |
 | 1234.589 W twai: bus error, REC=128                |  scrollable
 | 1234.601 I haus: 0x200 rank1 -> 62.14 degC          |  monospace 16
 | 1234.601 ! rank shift: rank 1 now 2876C25704E13CF9 |  amber
 | 1234.660 I haus: temp water-in 62.14 -> in[1]=1    |
 | 1235.002 W sd: flush took 412 ms                    |
 | ...                                                |
 +---------------------------------------------------+  740
 | 1 048 576 B | used 73% | dropped 21418 lines | up 3d|  status, 40 px
 |                                       [v FOLLOW]   |
 +---------------------------------------------------+  800
```

**Text view.** One `lv_obj` with `LV_OBJ_FLAG_SCROLLABLE` containing a single
`LV_LABEL_LONG_CLIP` label in a **custom monospace font** (16 px, generated with
`lv_font_conv`, ASCII `0x20-0x7E` plus `°` and the level glyphs). `lv_label` rather
than an `lv_list`, because a list means one object per line and there are thousands.

**Windowed rendering.** The label is never set to the whole 1 MB. Only the visible
window — about 44 lines at 16 px over 700 px — is rendered into the label, and the
window start is recomputed when the scroll position or the log head changes. This is
the same discipline as `label_set_text_cached()` in `26_CYD_CAN_monitor`, and for
the same reason: appending 1 MB of text to a label builds an invalidation backlog
that stalls `lv_task_handler()`.

**Follow at end.** Default on. When on, the view sticks to the newest record and
auto-scrolls as it arrives.

- A rebuild is throttled to **5 Hz**, not per line. A busy CAN fault emits hundreds
  of lines a second; without the throttle the screen would spend all its time in
  text layout and stop responding to touch entirely.
- **Any manual scroll switches follow off**, and a `v FOLLOW` button appears to
  switch it back on and jump to the end. This is the behaviour every log viewer has,
  and it is the one that matters: you scroll back to read something, and the view
  must not yank you to the bottom while you read it.
- Switching follow on re-renders the newest window immediately.

**Line lookup.** The ring is scanned for newlines only when the window start moves;
consecutive re-renders of the same window reuse the last line index. A full 1 MB
scan costs a few milliseconds and happens at most a few times per second.

**Colour.** By level: `E` red `0xFF5A5A`, `W` amber `0xFFB347`, `I` grey `0xC8C8C8`,
`D` dim `0x6A6A6A`. Two app-specific prefixes override it: `!` for rank shifts and
sensor-role warnings (amber, bold), `!!` for bus errors and watchdog trips (red).
The whole ring is colour-mapped at render time, so level filtering is free.

**Toolbar.** `INV` (colour inversion, moved here from the status strip), `CLR`
(clear the ring, with a confirm step — it destroys 1 MB of evidence), `SAVE` (dump
the ring to `/log_<timestamp>.txt` on the SD card, which is also what happens
automatically before a reset, §7.6), and a level filter row `E W I D`.

**Status bar.** Bytes used, percentage, **lines dropped** (the overwrite counter —
so the user knows how much history the 1 MB actually holds), uptime, a WS-connected
dot, and the follow button. Showing the drop count is what turns "the log is empty"
from a confusion into a diagnosis.

**No network dependency.** The screen reads the ring directly and never touches
`WiFi` or `tpl_server`. That is the point: it must work when the network is the
thing that is broken. The WebSocket sink (§7.6) is the preferred way to read the log
and this screen is the fallback for when WiFi is down too — three sinks, so there is
always exactly one that works.

**A `tail -f` client** belongs alongside the project as a 20-line script
(`tools/tail_ws.py`, stdlib `websockets` or a raw socket) that connects to
`ws://<hostname>.local:81`, pretty-prints the JSON records with ANSI colours and
saves the tail on `Ctrl-C`. Without it, the raw JSON on a socket is technically a
console and practically not one.

## 9. History Charts

### 9.1 Requirement

F8: every detail screen carries a history chart switchable over **1 day, 1 week,
1 month**. The charts are the reason the SD card is on the board, so they are a
first-class feature of this project, not an extra.

### 9.2 Time ranges and point budgets

Base resolution is one sample per minute (§7.4). At 480 px of chart width:

| Range | Samples | Bytes read | Samples / pixel column | What the column shows |
| ----- | ------- | ---------- | ---------------------- | -------------------- |
| **1 day** | 1 440 | 17 KB | 3 | min–max envelope + mean dot |
| **1 week** | 10 080 | 121 KB | 21 | min–max envelope + mean line |
| **1 month** | 43 200 | 518 KB | 90 | min–max envelope + mean line |

At three minutes per pixel even the one-day view is over-sampled, which is why the
chart is an **envelope**, not a polyline: a single 90 s thermostat cycle spans half
a pixel column in the month view and would be invisible in a plain average.

512 KB is comfortably inside the 8 MB PSRAM, so a raw month of records *could* be
cached wholesale. It is deliberately not (§9.5).

### 9.3 Record format and downsampling

Two-tier. The **store** is fixed-size binary; the **view** is a downsampled
envelope.

```
store:  data_202610.bin   43 200 x 12 B = 518 KB / month, append-only
        seek(index) = index * 12                          O(1)

view:   struct ChartColumn { uint16_t t; int16_t mn[4]; int16_t mx[4];
                             uint16_t mean[4]; uint8_t flags; };
        480 columns x ~18 B = ~8.6 KB per series set
```

Downsampling is done **streaming**, never by loading the window first:

```
for (chunk of 512 records:            # 6 KB working buffer
    read  -> Sample[512]
    for each sample:
        col = (sample.epoch - t0) * px / (t1 - t0)
        accumulate mn/mx/sum into ChartColumn[col]
finalise: mean = sum / n, flagged where n == 0
```

Peak RAM for a chart build is therefore **~15 KB** (6 KB chunk + 8.6 KB columns),
not 518 KB. This is what keeps N6 honest, and it means a 32 MB SD card is enough
for well over a year rather than eight weeks.

**Gaps are first-class.** A missing minute is not skipped silently: the column is
marked `GAP` and drawn as a dashed baseline rather than interpolated across. A
straight line between 03:00 and 05:00 would assert that the heater held its
temperature for two hours, which is precisely the kind of invented data a history
view exists to avoid.

### 9.4 Chart rendering

- **`lv_canvas`, not `lv_chart`.** `lv_chart` is line-only and draws its own axes,
  which cannot express a min–max envelope. A canvas gives direct control and lets
  the chart share the background's colour language.
- **Chart area** 440 × 260 at (20, 200). Rendered **once into a canvas** and
  refreshed only when the range changes or a new sample lands — i.e. at most once a
  minute. The live value in the header updates every second and invalidates only
  the header.
- **Y-axis auto-scale with hysteresis.** Range = `[floor(min - pad), ceil(max +
  pad)]`, rounded to a "nice" step (1 / 2 / 5 × 10ⁿ). Hysteresis: the axis only
  changes when the data leaves the current band by more than 12 %, otherwise a
  temperature oscillating around a boundary makes the whole graph jump every
  second. The axis labels are drawn once, into the same canvas.
- **Waterfall of pixels, not a filled polygon.** Each column is filled with
  ~20 stacked 1 px rects whose colour is interpolated from the accent colour at the
  value to the background colour at the baseline. That gives a vertical gradient
  for free where a polygon fill would give a flat one, at a cost that is trivial
  because the canvas is only redrawn once a minute.
- **Envelope rendering**: for 1 day, per-column min and max are drawn as a 1 px
  vertical tick; for 1 week and 1 month, they are shaded as a band at 25 % alpha.
  A line for the mean runs over the top.
- **Reference line** (chamber target) is dashed, drawn on the canvas, and labelled
  at the right edge.

### 9.5 Reading windows off the SD card

Because records are fixed-size, a window is a `seek` and a sequential read:

| Range | Start index | Read |
| ----- | ----------- | ---- |
| 1 day | `count - 1440` (clamped at 0) | 17 KB |
| 1 week | `count - 10080` | 121 KB |
| 1 month | `count - 43200` | 518 KB |

Reading 518 KB from a microSD card at ~2 MB/s costs roughly 250 ms — perceptible,
so:

- **The build runs on the recorder task**, not in `loop()`. The UI sets a "chart
  loading" flag, draws it, and picks the result up when the task posts it. The home
  screen never stutters.
- **Built charts are cached**, keyed by `(range, end_index)`, in a small LRU of
  four. Tapping back and forth between 1 day and 1 month is instant after the
  first visit, which is what a user actually does when comparing a spike against a
  month.
- **The recorder must have flushed** before any read. If the card is absent or the
  file is missing, the chart area shows "no data" — never an empty axis that reads
  as "nothing happened".

### 9.6 Chart interaction

| Interaction | Result |
| ----------- | ------ |
| Range segmented control | Rebuild for 1 day / 1 week / 1 month |
| Tap-and-hold on the chart | Vertical scrubber line plus a readout bubble with the exact timestamp and min/mean/max at that column |
| Drag while holding | Scrub continuously; the bubble follows |
| Release | Scrubber clears after 1.5 s |
| Tap the mean-value footer | Toggle absolute / relative (°C vs delta) on the water screens |

The scrubber is implemented as an `lv_canvas` overlay plus one `lv_label`, moved on
`LV_EVENT_PRESSING`. Because the chart columns are already indexed by pixel, the
scrubber is a lookup — no interpolation, no re-read, no extra RAM.

Touch precision at 480 px means one pixel column is ~3 minutes at 1 day and ~90
minutes at 1 month. The bubble therefore shows the *column's* aggregate, not a
fabricated instant value. Claiming a precise reading the data cannot support is how
history views lose trust.

### 9.7 File rotation and retention

- One file per month: `/data_YYYYMM.bin`, named by the wall clock at creation.
- Rotation is decided by the recorder task when the file's implied month differs
  from now — derived from the last sample's epoch, so an NTP step cannot produce a
  file with a nonsense name.
- **Retention: 24 months**, configurable in SPIFFS config. On startup and daily,
  the oldest files beyond the limit are deleted. Deleting is never more than one
  file per pass, so a card with 30 stale months does not stall the recorder.
- **Free-space guard:** if free space drops below 50 MB, retention is forced to
  3 months and the home screen's SD dot turns amber. Losing a month of history is
  acceptable; filling the card and losing *everything* is not.
- **Card removal mid-write:** the SD driver drops, the recorder sets a fault flag
  and retries every 30 s. Samples during the outage are simply absent — which §9.3
  renders as gaps, correctly.
- **A CRC per block.** Records are appended in blocks of 64 with a trailing CRC32,
  so a power cut during a write is detected at read time and the affected tail is
  discarded instead of being plotted as corrupt data.

## 10. Threading, Timing and Performance

### 10.1 Task map

| Task | Core | Priority | Stack | Period | Owns |
| ---- | ---- | -------- | ----- | ------ | ----- |
| `loopTask` | 1 | 1 | 8 KB | ~5 ms | `lv_task_handler`, UI only |
| `canTask` | 0 | 3 | 4 KB | event-driven (100 ms alert timeout) | TWAI, `haus_state` writes |
| `recorderTask` | 0 | 2 | 6 KB | 1 s (samples on the minute) | SD write, chart builds |
| `tpl: wifi` | 0 | 0 | 2688 B | 100 ms | template |
| `tpl: websocket` | 0 | 1 | — | 750 ms | template — **also drains the log ring** (§7.6) |
| `tpl: watchdog` | 0 | 1 | — | template | template |
| `tpl: webserver` | 1 | 1 | 4 KB | 10 ms | template |
| `tpl: command` | 1 | 1 | — | template | template |

The single hard rule, inherited from `26_CYD_CAN_monitor`: **LVGL is touched only
from `loopTask`.** Every other task communicates by writing a snapshot or setting a
`volatile` flag. A CAN task that called `lv_label_set_text()` directly would corrupt
the object tree; a command callback that did so would do it from another core while
`lv_task_handler` is mid-frame.

Logging is the one subsystem written by *every* task, so its concurrency rules are
stated explicitly: the ring write is the only shared mutation, it is guarded by a
mutex taken with a **5 ms timeout** and drops the record on failure rather than
blocking the caller. A full ring therefore costs a bounded amount of time per
dropped line and can never become a deadlock — a logging path that can hang the
system it is meant to diagnose is worse than no logging.

### 10.2 State hand-off

```cpp
struct HausState {          // POD, trivially copyable
  int16_t temp_c100[HAUS_CAN_MAX_SENSORS];   // HAUS_CAN_TEMP_EMPTY == absent
  uint16_t pwm_permille;
  uint8_t  pwm_src;
  int16_t  power_w;
  uint8_t  power_src;       // NONE / SML / ADC
  uint32_t rx_frames, tx_frames, bus_errors;
  uint32_t last_frame_ms;
  uint8_t  can_fault;       // sticky until a frame is received again
  uint8_t  sd_fault;
};

extern HausState g_state;         // written by canTask only
extern volatile uint32_t g_state_seq;   // bumped on every write
```

`loopTask` copies `g_state` into a local at the top of each iteration. A torn read
is possible in principle and harmless in practice — every field is independently
meaningful and the UI redraws 10× a second — but the copy makes the *contract*
explicit, so a future field that is not atomic cannot silently tear.

### 10.3 Refresh budget at 480 × 800, RGB565

| Work | Frequency | Cost |
| ---- | --------- | ---- |
| Header + clock | 1 Hz | ~1 % of a frame |
| Flame animation | 20 Hz, ~120 × 140 px invalidated | ~3 % |
| Pipe flow dashes | 20 Hz, ~2 × 60 × 24 px | ~1 % |
| Chart rebuild | 1 / min, full 440 × 260 | ~8 % burst on the recorder task |
| Background image | never redrawn | 0 |

A full-screen flush at 16 MHz pixel clock is 480 × 800 × 16 bit / 16 MHz ≈ **38 ms**.
At 20 Hz that is 77 % of a core just for the animated regions — which is why only
small regions are invalidated and why the background is a cached image. Anything
that forces full-screen invalidation (a `lv_obj_invalidate` on the screen, a theme
change) will visibly stutter and is called out in review.

LVGL's `LV_DISPLAY_RENDER_MODE_DIRECT` with partial buffer support handles this; the
draw buffers are sized `TPL_WS7_DRAW_LINES` lines and live in internal SRAM.

### 10.4 Timing budget

- **Home screen redraw:** ≤ 5 ms typical.
- **Touch → visual feedback:** ≤ 2 frames (< 100 ms). Anything slower reads as a
  broken panel.
- **Screen transition:** 120 ms cross-fade on the content layer.
- **Chart build (1 month):** ~250 ms on the recorder task; the UI shows a spinner
  in the chart area, never a frozen frame.
- **Idle return to home:** 3 minutes.

## 11. Risks and Mitigations

| # | Risk | Impact | Likelihood | Mitigation |
| - | ---- | ------ | ---------- | ---------- |
| R1 | Enabling CAN kills USB, so a firmware bug becomes a physical power-cycle to recover | High | High | OTA is the production path (§7.5); `USB_SEL` is low at boot (§3.3 R1); BOOT+power-cycle always reflashable; crashes logged to SD, not just serial |
| R2 | `Serial` is `HWCDC`, so all logging vanishes exactly when CAN is on | High | Medium | Mitigated three ways: the PSRAM ring is authoritative and needs no serial; the WebSocket sink gives a desk console; the log screen gives an on-panel console. Remaining question is only whether `uart_set_vprintf` can also capture the port's own output (§12 Q2) |
| R2b | Writing to an unenumerated USB CDC blocks the logging task, turning "no console" into "hung system" | High | Medium | `tpl_log_set_serial(false)` at CAN enable (§7.6); ring writes use a 5 ms mutex timeout and drop rather than block |
| R2c | WebSocket writes from the logging path stall on a paused client | Medium | Medium | The ring is the buffer; the drain happens on the existing WS task at 750 ms with an 8 KB/tick cap (§7.6) |
| R2d | A log storm makes the panel unresponsive | Medium | Medium | 5 Hz on-screen rebuild, WS backpressure markers, level filters, dropped-line counter visible (§8.12, N12) |
| R3 | RGB flush bandwidth makes the UI stutter at 800 × 480 | Medium | Medium | Partial redraw only, cached background, line buffers sized by build flag; measure in phase 5 (§13) |
| R4 | LVGL 9.2 rotation silently no-ops without `LV_DRAW_TRANSFORM_USE_MATRIX` | High | Medium | Assert the macro at compile time in `tpl_ws7_display()`; a wrong screen orientation is otherwise very hard to diagnose |
| R5 | `HAUS_CAN_TEMP_EMPTY` (`INT16_MIN`) plotted as a real temperature | High | Medium | Sentinel checked at the decoder boundary; empty slots render grey and are excluded from chart statistics |
| R6 | History gaps interpolated, asserting data that was never measured | Medium | Medium | Gaps marked and drawn as gaps (§9.3); never bridge them |
| R7 | `tpl_sd` and the recorder fight over the card (open/read across a write) | High | Medium | Single ownership: recorder writes, `tpl_sd` reads only after a flush barrier (§6.7) |
| R8 | SD card fills and silently stops recording | Medium | Medium | Free-space guard, retention floor, amber status dot (§9.7) |
| R9 | No PWM backlight — the panel cannot dim, unlike the CYD boards | Medium | High | Idle dimming is impossible; mitigate with content-driven dimming (dark theme already) and a screensaver that fades the *content* to black, leaving the backlight on. Accept the trade |
| R10 | Refactoring `26_CYD_CAN_monitor` breaks project 26 | High | Medium | Extract behind a stable API; project 26 must keep building and passing (§7.3 step 5). It is the regression test |
| R11 | Host harness diverges from the device because of `#ifdef` drift | Medium | Medium | All conditional code lives below `services/`; the harness builds the *same* app/ and ui/ sources, never a copy. A CI job builds both targets on every change |
| R12 | Decoded background image (768 KB) plus draw buffers exhausts PSRAM/internal RAM | Medium | Medium | Static allocation, no heap; budget in §10.3; assert free heap after init in phase 1 |
| R13 | NTP not yet synced at boot → clock and chart axis are wrong | Low | Medium | Gate the clock on the NTP flag; show `--:--` rather than 1970. Charts start from the first valid sample |
| R14 | Touch calibration wrong in portrait, so zones are offset | Medium | Low | GT911 is already panel-native (§6.4), so no calibration table exists to be wrong; verified by the four corner taps in phase 6 |
| R15 | A sensor is unplugged, ranks shift, and the home screen shows a right number under a wrong label | High | Medium | F14 / §7.7: resolve `slot → rank → ROM → function`; rank→ROM is learned from identity frames and **expires** after 90 s; a shift is reported, never silently relabelled |
| R16 | No water-out sensor exists, so the third zone has no honest source | Medium | **Certain** | Q9 must be resolved before phase 6. Until then the zone shows `—`, never rank 2's board temperature |
| R17 | `tpl_debug_log` changes break the projects that already use it | Medium | Low | Opt-in via `USE_TPL_DEBUG_LOG`; keep `DEBUG_LOG_LINES`/`DEBUG_LOG_LINE_SIZE` working as aliases for the byte-size defaults so existing builds are unaffected |

## 12. Open Questions

Each of these changes the design, so none is silently decided above.

### 12.1 Resolved

| ID | Question | Resolution |
| -- | -------- | ---------- |
| ~~Q3~~ | Which sensor rank is chamber / water-in / water-out? | **Answered**, and the framing was wrong on the first pass. Rank is *not* bound to a function. The chain is `slot → rank → sensor ID (DS18B20 ROM) → function`: rank is a position in the sorted list of *currently present* sensors, so it shifts on unplug, while the function belongs to the sensor and travels with the probe. The `AVR/01_heizung_temp` README table (rank 0 = heater element → chamber, 1 = supply side → water in, 2 = rf-nano board → board) documents the **current installation only**. Consequences recorded in §7.7: rank→ROM is learned from the `0x180+r` identity frames and expires; ROM→function is local SPIFFS config and editable, because hardcoded ROMs would make the panel work on exactly one machine. Side effect: the current installation has no return-line sensor, so there is no water-out function to bind — that became **Q9**. |

### 12.2 Open

| ID | Question | Impact | How it gets answered |
| -- | -------- | ------ | -------------------- |
| **Q1** | Is "current power" the household total from SML, or the heater's own draw from `0x2C0`? | Chooses the `PowerSource` implementation; decides whether a semantic SML decoder has to be written | Jochen. Recommendation on record: SML primary, ADC fallback, badge in the UI |
| **Q9** | **There is no water-out temperature on the bus.** Where does the third zone's data come from? | Decides whether the third temperature is real | Options: (a) fit a 4th DS18B20 on the *return* line at the AVR board — its rank is whatever the sort produces, and only its ROM binding needs adding here; the honest fix, cheap, and the AVR side already handles four sensors; (b) derive it from water-in and the PWM duty, which is a model, not a measurement, and must be labelled as such; (c) make the third zone show rank 2's board temperature and rename it "Board", which abandons the water-out concept. **Recommend (a)**; the panel code needs no change for it, only the ROM binding |
| **Q2** | Does the Arduino core open `Serial` on `HWCDC` (USB) or `UART0` for this S3 variant? | Decides whether option B of §7.6 works — i.e. whether the ring captures `Serial.println` from the template, or only `esp_log` and app-level `TPL_LOGx` | Read on hardware at the bench, phase 1. Check the `HWCDC`/`HardwareSerial` instantiation and the S3 USB-serial-JTAG default |
| **Q4** | Are the SD and the history log allowed to share the internal SPIFFS, or must assets move to the SD? | Determines whether the background image is in SPIFFS (512 KB) or SD | Recommend SPIFFS for the PNG (it must be readable when the SD is being written to, and a card swap must not take the background with it) |
| **Q5** | Does the current `tpl_sd` need to keep working on this board at all, given the recorder supersedes it? | Could delete ~200 lines from `tpl_sd` for this project, or keep the file browser for convenience | Recommend keeping `/sd/list` + `/sd/download` — browsing the recorded history over HTTP is genuinely useful |
| **Q6** | Retention: 24 months at 518 KB/month is 12 MB — comfortable. Is more wanted? | Storage and the free-space guard threshold | Jochen; default 24 |
| **Q7** | Should the panel also *write* to the bus — e.g. acknowledge a timestamp request or set a display-brightness register? | Adds TX paths and a write policy for a shared bus | Recommend no: this is a listener. Revisit if a second wall panel needs clock sync |
| **Q8** | Is EEZ Studio in scope for the home screen? | Either hand-authored LVGL (faster, more control, matches project 26's current state) or generated (draggable, but cannot express the flame/pipes) | Recommend hand-authored; EEZ cannot produce the vector heater or the envelope chart |
| **Q10** | Should the WebSocket log sink be authenticated? | `tpl_websocket` is unauthenticated on the LAN today; log content is house telemetry | Recommend accepting it on a home LAN, consistent with the existing unauthenticated `/update` OTA endpoint. If the answer is no, the change belongs in `tpl_websocket` for all projects, not here |
| **Q11** | Should `tpl_debug_log`'s PSRAM ring be made the template default for **all** projects, or stay opt-in? | Affects every existing project's RAM footprint | Recommend staying opt-in for now, defaulting on for boards with no console once the module is proven |

## 13. Implementation Plan

Each phase ends in something that builds and runs. Phases 1-3 are Deliverable A and
are usable by any future project; 4-8 are Deliverable B.

| Phase | Content | Exit criterion |
| ----- | ------- | -------------- |
| **0** | This white paper; peer white paper for the board | Reviewed; Q1-Q8 answered |
| **1** | `tpl_waveshare_lcd7`: CH422G bring-up, LCD reset, backlight, serial/UART verification | Panel lights, serial logs, R2 answered |
| **2** | `esp_lcd_rgb_panel` + LVGL display in all four rotations, with the rotation assert (R4) | Coloured test pattern, correct in every orientation |
| **3** | GT911 touch in portrait; corner-tap calibration test (R14) | All four corners and all three zones hit reliably |
| **4** | Project skeleton, `platformio.ini`, own `lv_conf.h`, partitions, OTA confirmed on hardware; `Serial`/UART question answered (Q2) | OTA updates a running build over WiFi with CAN enabled |
| **5** | `tpl_debug_log` → PSRAM byte ring, detachable serial, WebSocket sink, `/log` serving the ring, `tools/tail_ws.py` | 1 MB ring fills and wraps; `tail_ws.py` shows live records on the Mac; `tpl_log_set_serial(false)` does not hang the system |
| **6** | `haus_state`, CAN task, `haus_can.h` decoding, **ROM-identity role resolution (§7.7)**, host harness stubs | Home screen shows live values from a replayed frame log, on macOS, no hardware; a simulated rank shift is reported, not silently relabelled |
| **7** | Home screen: background image, clock, vector heater graphic, power readout, status strip, zones | Runs on the panel; measured against the §10.3 budget |
| **8** | Recorder service: SPI backend, host backend, rotation, retention, CRC | A month of history appears on the SD card and survives a power cut mid-write |
| **9** | Detail screens + envelope charts at 1 day / 1 week / 1 month + scrubber | All four screens, all three ranges, on hardware and on macOS |
| **10** | **Log screen** (§8.12): monospace windowed view, follow-at-end, level filters, SD dump | Readable with WiFi down and USB disabled; a 5 000-line burst does not make the panel unresponsive |
| **11** | `tpl_can_monitor` extraction from project 26; gear screen | Project 26 still builds and passes; the gear opens the scope |
| **12** | Polish: idle return, colour inversion moved to the log toolbar, dither/glow assets, soak test | Runs 72 h without a watchdog reset or a leak |

Sequencing note: **phase 5 before phase 7** is deliberate. Building the host harness
against a stub decoder first means the entire GUI, including every later screen and
all the chart maths, is developed without flashing. That is the difference between
a two-week build and a two-month one.

Second sequencing note: **phase 5 before phase 4's tail end.** Bringing up the log
ring before the display means that if the display fights back — and an RGB panel at
800 × 480 will — there is somewhere to read the error other than a USB console that
is already gone.

## 14. Appendix

### A. Related Documents

| Document | Location |
| -------- | -------- |
| This white paper | `ESP32/28_hauszentrale/docs/whitepaper.md` |
| Board reference, pinout, examples | `boards/WAVESHARE-ESP32-S3-Touch-LCD-7/README.md` |
| CYD abstraction (the pattern `tpl_waveshare_lcd7` mirrors) | `ESP32/prj_template/docs/cyd_board_abstraction.md` |
| Board abstraction white paper | §6 here; becomes `ESP32/prj_template/docs/waveshare_lcd7_board_abstraction.md` at implementation time |
| CAN protocol — single source of truth | `include/haus_can.h` |
| CAN monitor design notes | `ESP32/26_CYD_CAN_monitor/docs/protocol_detail_screen_design.md` |

### B. Vendor References

- Product: <https://www.waveshare.com/esp32-s3-touch-lcd-7.htm>
- Docs: <https://docs.waveshare.com/ESP32-S3-Touch-LCD-7>
- Examples: <https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7>
- Panel: ST7262 · Touch: GT911 · Expander: CH422G

### C. Decision Log

| # | Decision | Alternative rejected | Rationale |
| - | -------- | -------------------- | --------- |
| D1 | New `tpl_waveshare_lcd7` module | Extend `tpl_display` | Different bus, driver, touch model and peripheral control (§6.1) |
| D2 | LVGL ≥ 9.2, matrix transform for rotation | Re-program the panel | `lv_display_set_rotation()` is the only path that avoids a second set of timings; needs 9.2+ (N1) |
| D3 | Two line buffers in internal RAM, `DIRECT` render mode | Full-screen PSRAM buffer | PSRAM is not DMA-capable here; full-screen buffers also stall the 38 ms flush |
| D4 | Background PNG in SPIFFS, decoded once to static PSRAM | Embed the raw RGB565 blob | 768 KB against a 3 MB shared app slot; PNG is ~120 KB and re-encodes cheaply |
| D5 | Vector heater via `lv_canvas` | A bitmap with baked-in colours | The flame and pipes must react to live values; a bitmap can only go stale (§8.4) |
| D6 | Invisible `lv_obj_t` overlay buttons as zones | Coordinate tests in the read callback | Independent of the drawing; LVGL handles press, click and focus (§8.5) |
| D7 | Fixed 12-byte records | Text CSV, as `15_power_log_eink` uses | Enables `seek()` — a month window is a seek, not a scan (§7.4, §9.5) |
| D8 | Streaming downsample to a per-pixel envelope | Cache the whole month in PSRAM | Bounds RAM at ~15 KB instead of 518 KB, and keeps N6 true (R12) |
| D9 | Gap markers rather than interpolation | Connecting points across a dropout | Asserting measurements that were never taken is the failure mode a history view must not have (R6) |
| D10 | `AppState` snapshot + sequence counter | Direct global reads from the UI | Makes the threading contract explicit and keeps the host build honest (N5, §10.2) |
| D11 | Recorder owns writes, `tpl_sd` owns reads, flush-barrier between | Extend `tpl_sd` with a write path | `tpl_sd` is read-only by design and its CS model does not fit the CH422G (§6.7, R7) |
| D12 | Hand-authored LVGL for all screens | EEZ Studio | EEZ cannot express the vector heater or an envelope chart, and project 26 already left it (Q8) |
| D13 | Extend the existing `tpl_debug_log` | A new project-local logging module | It already exists, is opt-in, and is the natural home; extending it fixes it for every future console-less board (§7.6) |
| D14 | 1 MB **PSRAM byte ring**, newline-delimited | The current fixed 100 × 160 B internal-RAM line array | PSRAM is there and unused by anything else; a byte ring wastes nothing on short lines and truncates nothing important |
| D15 | Three sinks — ring (authoritative), Serial, WebSocket, panel | Writing to the console as the primary path | One copy, derived views. The CAN/USB conflict then stops being a debugging problem at all |
| D16 | WebSocket drained **from** the existing WS task, 8 KB/tick cap | Pushing to sockets from the log path | Decouples logging from a client's TCP window; zero cost when no client is attached (R2c) |
| D17 | Follow-at-end on by default, **disabled by any manual scroll** | Always following | Reading something must not be interrupted by the log yanking the view away; this is what every log viewer does and it is why people trust them |
| D18 | Log rendered as a windowed slice (~44 lines), rebuilt at 5 Hz | Appending to one growing `lv_label` | 1 MB of text in a label is an invalidation backlog that stalls `lv_task_handler` — the same trap `26_CYD_CAN_monitor` documented |
| D19 | Resolve `slot → rank → sensor ID → function`; learn rank→ROM from the identity frames, expire it, keep ROM→function in editable SPIFFS config | Caching "rank 1 = water in", or hardcoding ROMs in firmware | Rank is a derived address that shifts on unplug; functions belong to the sensor (R15). Hardcoded ROMs would make the panel correct on exactly one machine |
| D20 | Third zone shows `—` until a water-out sensor exists | Displaying rank 2's board temperature as "water out" | Rank 2 measures the transmitter PCB, not water. A plausible wrong number is worse than an honest gap (R16, Q9) |
| D21 | Long-press the status strip opens the log screen | Keeping colour inversion there; burying the log behind the gear | When serial is gone and the network is suspect, the log is the first thing one reaches for |

### D. Terminology

| Term | Meaning here |
| ---- | ------------ |
| Chamber | The firebox / combustion chamber temperature (`0x200`, slot per Q3) |
| Water in / water out | Flow temperature entering and leaving the heat exchanger |
| Gear | Bottom-right button opening the CAN monitor |
| Sensor ID | The DS18B20 ROM. Unique and permanent; a temperature's function belongs to it |
| Rank | Derived position in the sorted list of *present* sensors. A transient address that shifts on unplug |
| Log screen | Full-screen console; long-press the status strip (§8.12) |
| Column | One pixel column of a chart, holding min/mean/max for that time slice |
| Envelope | The min–max band drawn per column, as opposed to a single mean line |
| `USB_SEL` | CH422G IO4 — the USB ⇄ CAN mux. HIGH means CAN and no USB |

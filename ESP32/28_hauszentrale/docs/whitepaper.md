# White Paper: 28_hauszentrale — House Central on the Waveshare ESP32-S3-Touch-LCD-7

**Status:** COMPLETE — all sections written. Open questions in §12 remain to be answered before phase 1.
**Revision:** 1.5 — applies the rev 1.0 review and retires `docs/review.md`. The expander map in §3.2 now matches §3.4, the new template `.cpp` is guarded, `platformio.ini` pins octal PSRAM and UART0, the flush budget is 24 ms, GPIO 2/4/17 are not aliased as LEDs, and the AHT20/CH422G address collision is recorded (Q24, R30)
**Revision:** 1.4 — adds the bench verification of the physical board (§3.1.1: ESP32-S3 rev v0.2, **16 MB** flash correcting the 8 MB identity, MAC `28:84:85:a7:a6:78`, CH9102 USB-UART bridge, the mechanical UART switch, and the `rtc_pcf85063` / IDF 5.5.4 demo currently on the flash), the mechanical-switch constraint on §3.3 and the §7.5 runbook, D49–D51, Q22–Q23
**Revision:** 1.3 — adds the memory map (§10.5), the three-tier history store (§7.10), the modelled water-out temperature (§7.11), the CAN enable switch (§7.12, §8.14), the 4 mm type scale (§8.1.1) and the bottom nav pane (§8.11)
**Revision:** 1.2 — adds the living-room AHT20 + BMP280 local I²C sensors (§3.5, §7.8, F15–F17, N13–N14, §8.13), heater/pump PWM duty control (§3.6, §7.9, F18–F21, N15–N16, §8.6.1), and the declarative-UI analysis (§4.4, D31–D32)
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
| Local sensors | BMP280 (I²C `0x76` or `0x77`) on H1. An AHT20 at `0x38` collides with CH422G WR-IO — Q24, R30 |
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
- Board constants in `tpl_system.h`. This board defines **none** of `tpl_ledPin`, `tpl_ledPin_R/G/B` or `tpl_backlightPin`: GPIO 2 is RGB DATA12, GPIO 17 is DATA3, and GPIO 4 is reserved for Q17/Q19 (§6.7).
- Symlink regeneration + `make links` integration so every project can adopt it.

The board chapter of this document (§6) **is** the Deliverable-A white paper. When
the module is written it is split out to
`ESP32/prj_template/docs/waveshare_lcd7_board_abstraction.md`, following the
precedent of `cyd_board_abstraction.md`, and §6 is replaced by a pointer.

**B — Project `ESP32/28_hauszentrale`**

- `platformio.ini` with `esp32ota` + `esp32usb` envs, S3 / octal PSRAM / **16 MB**
  flash (measured, §3.1.1 — not the 8 MB the part was originally listed as).
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
- **Local climate sensor:** a BMP280 (temperature + pressure) on the board's H1
  I²C header, read directly by the panel — not on the CAN bus, and therefore
  outside the F14 resolution chain (§3.5, §7.8, §8.13). Humidity wants a second
  part; an AHT20 cannot share this bus (Q24, R30).
- **On-panel diagnostics:** extend `tpl_debug_log` to a 1 MB PSRAM ring, plus a
  scrollable log screen with follow-at-end (§7.6, §8.12).
- Function resolution through sensor ID, never through CAN rank (F14, §7.7) — rank is a
  shiftable address, the function belongs to the probe.
- OTA over WiFi (primary), with the USB/CAN mux consequence handled explicitly.
- **Heater and heating-pump control:** the panel is the *controller* of record
  for the PWM duty on `CAN_ID_PWM_SET` (`0x240`), which drives both the heater
  element and the circulation pump from the single existing channel. Step 1 is
  a duty slider on the chamber detail screen, 0 → 1000 permille, behind an
  explicit confirmation (§7.9, §8.6, F18–F21).

### 2.2 Out of Scope

- **Any safety interlock, watchdog or ramp on the actuator.** The panel may
  *command* a duty; it is not the thing that makes commanding safe. Ramping,
  minimum on/off pulse, upper-bound clamping and fault handling all belong to
  `AVR/01_heizung_temp`, which already owns them (§7.9, N15, R20).
- **Closed-loop heating control.** The panel sets a setpoint a person chose. It
  does not decide when the house is warm enough; that remains the AVR
  controller's job.
- **Writing anything but `CAN_ID_PWM_SET` on `0x240`.** No acknowledgements, no
  configuration registers, no addressing of other nodes (§7.9).
- **Redesigning `haus_can.h`.** Existing IDs stay; additions go in there repo-wide.
  Note that heater and pump are **one** control point on the existing
  bidirectional `0x240` / `0x280` pair, so this project needs no protocol
  change at all.
- **Telegram bot / Matter.** `tpl_template` modules that this board does not need.
- **Configuring the local sensors.** The AHT20 and BMP280 are read-only to the
  panel: their oversampling, filter and IIR settings stay at power-on defaults
  (§7.8). Reconfiguring them is a bench job, not a UI feature.
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
| Flash / PSRAM | **16 MB** quad flash, 8 MB **octal** PSRAM — flash size corrected from 8 MB, §3.1.1 |
| Display | 7" TFT, **800 × 480**, RGB565 parallel, 16-bit data bus |
| Panel driver | ST7262 |
| Touch | GT911, capacitive, I2C, 5-point |
| IO expander | CH422G. Four command addresses, not one slave: WR-OC `0x23`, WR-SET `0x24`, RD-IO `0x26`, WR-IO `0x38` |
| CAN | TJA1051 transceiver behind an FSUSB42UMX analog switch |
| Storage | microSD/TF slot, SPI, chip-select via CH422G |
| Extras | RS485 (external converter board), battery connector, BOOT button |

Full datasheets, schematic and community projects are catalogued in
`boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/`. Pin numbers in this document are
taken from `source/waveshare_lcd_port.h`, not from the board README's prose table,
which is off by one against that header and against its own register example (§3.4).

Two facts dominate every architectural decision that follows:

1. **The display is RGB parallel, not SPI.** It occupies 20 GPIOs and its pixel
   clock is generated by the LCD peripheral. `tpl_display` (TFT_eSPI + XPT2046)
   is structurally incapable of driving it.
2. **The board has a real CAN transceiver but hides the USB behind a switch.**
   The consequence is in §3.3 and it dictates the entire OTA story.

### 3.1.1 Bench Verification — 2026-10-16

Measured on the physical board with `esptool` 5.6.0 and pyserial, not read off a
datasheet. Everything here is reproducible:

```bash
esptool -p /dev/cu.usbmodem5C941561291 --chip esp32s3 chip-id
esptool -p /dev/cu.usbmodem5C941561291 --chip esp32s3 flash-id
esptool -p /dev/cu.usbmodem5C941561291 --chip esp32s3 read-flash 0x8000 0x5000 /tmp/part.bin
```

| Property | Measured | Note |
| -------- | -------- | ---- |
| Chip | ESP32-S3 (QFN56), **revision v0.2**, dual core + LP core, 240 MHz | Matches the R&D sample, not silicon revision 0.0 |
| Crystal | 40 MHz | |
| MAC | `28:84:85:a7:a6:78` | Reserve it in the DHCP server; it is the panel's identity for the whole project |
| Flash | 16 MB, Winbond `4018`, quad, 3.3 V, 4 data lines | **Corrects the 8 MB in the identity table above.** `platformio.ini` must say `board_build.flash_size = 16MB` or the image is capped at half the part |
| PSRAM | 8 MB, `AP_3v3` | Confirms the octal PSRAM and the 3.3 V rail, i.e. the H2 jumper question (Q13) matters for the sensors |
| USB | enumerates as **WCH CH9102**, VID:PID `1A86:55D3`, SN `5C941561291` | A USB-**UART** bridge, not native USB CDC — see below |
| Boot (BOOT released) | `rst:0x1 POWERON, boot:0x8 SPI_FAST_FLASH_BOOT`, app runs | Normal path |
| Boot (BOOT held) | `rst:0x1 POWERON, boot:0x0 DOWNLOAD(USB/UART0)` | ROM download mode; `esptool` connects immediately |

**The USB path is a CH9102 UART bridge, which answers half of Q2.** No Espressif
device (`VID 0x303A`) is present on the bus at any time — not even when the board
sits in ROM download mode, which is when native USB CDC would be visible. The
console therefore arrives on GPIO 43/44 through a serial bridge, which is what R3
requires. What is *not* yet answered is which port the Arduino core opens: that
depends on the demo firmware, not on the silicon, and Q2 stays open for phase 1.

**There is a physical UART switch, and it is not in this document's model.** The
board carries a mechanical switch that routes the CH9102 between the ESP32 and the
CAN side. With it in the wrong position the port still enumerates on macOS, still
opens, and returns **zero bytes at every baud rate and every DTR/RTS combination** —
including `esptool`'s ROM sync, which reports `Failed to connect: No serial data
received`. That failure is indistinguishable from a dead board, a wrong cable or a
bad driver. The first thing to do with a silent board is now known: **throw the
switch**, then re-probe. §3.3's model has the mux under CH422G IO5 control; whether
the mechanical switch is in addition to that, or *is* that, from another side, is
**Q22**.

**What is currently on the flash.** Not a blank part and not a 28_hauszentrale
build — an unrelated demo:

| Partition | Offset | Size | Label |
| --------- | ------ | ---- | ----- |
| app | `0x00009000` | 24 KB | `nvs` |
| app | `0x0000f000` | 4 KB | `phy_init` |
| app | `0x00010000` | 5120 KB | `factory` |

`esp_app_desc_t` at `0x10020` reports project **`rtc_pcf85063`**, version `1`,
built `Jun 25 2026` with **ESP-IDF v5.5.4**. Three consequences:

1. **IDF 5.5.4 is proven to run on this board.** Q21 asks for Arduino 3.3+
   (IDF 5.3) or plain `esp-idf`; a working 5.5.4 toolchain on this exact silicon
   is evidence for the second option that was previously only theoretical.
2. **The partition table is single-app with a 5 MB factory slot and no OTA
   partitions.** Whatever this project defines, it defines from scratch — and
   `pio run --target uploadfs` plus ArduinoOTA (§7.5) will lay out the same way.
3. **The flash must be erased before the first 28_hauszentrale flash**, not
   merged into. There is no user data on this board to preserve.

The demo prints `WIFI is failure!` followed by a free-RAM map on boot, which is the
expected shape of a foreign app with no credentials — it is not a defect in the
board, the display or the antenna.

### 3.2 Resources and Pin Budget

Direct GPIOs consumed by the display alone: HSYNC 46, VSYNC 3, DE 5, PCLK 7, and
DATA0–DATA15 = 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40.

Everything else must fit in what remains, which is why the CH422G exists.

| Peripheral | Pins | Control |
| ---------- | ---- | ------- |
| Touch (GT911) | SDA 8, SCL 9 | I2C @ 400 kHz, shared with CH422G |
| CAN (TJA1051) | TX 19, RX 20 | behind FSUSB42UMX, gated by CH422G **IO5** |
| SD (SPI) | MOSI 11, MISO 13, SCLK 12 | CS via CH422G **IO4** (active low) |
| LCD reset | — | CH422G **IO3** |
| Backlight | — | CH422G **IO2** → AP3032 **EN** pin via R52 (**on/off only, no PWM** — §7.13) |
| Touch reset | — | CH422G **IO1** |
| Climate sensors | SDA 8, SCL 9 | I²C, on the **H1** header — §3.5. An AHT20 at `0x38` collides with WR-IO; see §7.8 and Q24 |

The backlight being binary rather than dimmable is a real UX regression against the
CYD boards, which have LEDC PWM. §11 carries it as a risk.

### 3.3 The USB / CAN Multiplex (critical constraint)

The board routes ESP32-S3 **GPIO 19 and GPIO 20** through an FSUSB42UMX analog
switch. On the ESP32-S3 those two pins *are* the native USB D− and D+ pair. The
switch therefore selects between:

| `USB_SEL` (CH422G **IO5**) | GPIO 19/20 connected to | Consequence |
| -------------------------- | ------------------------ | ----------- |
| LOW                       | USB-C connector | USB CDC / serial works, **CAN dead** |
| **HIGH — power-on default** | TJA1051 transceiver | **CAN works, USB dead** |

There is no third state and no software workaround: while CAN is enabled the
device cannot be flashed over USB, cannot be monitored over USB, and cannot receive
a USB-serial reset.

**Add to this: a mechanical switch.** §3.1.1 found a physical switch on the board
that routes the CH9102 UART bridge, and in the position it was first probed in the
bridge enumerated, opened, and delivered **nothing** — no boot log, no ROM sync,
at any baud. The CH422G/FSUSB42UMX analysis below still holds and is still the
safety argument, but the *operator* also has a switch to set, and getting it wrong
looks exactly like a dead board. Two consequences: the runbook in §7.5 gains a
"throw the switch" step, and whether the switch and IO5 are two views of one mux or
two independent gates is **Q22** — it decides whether N19 is sufficient or whether
firmware also has to fight a mechanical default.

This is why §7.5 exists, and it drives three design rules:

- **R1 — Do not treat the expander's reset value as the chip's power-on level.**
  Rev 1.0 said the CH422G powers up with `USB_SEL` low, so a bricked board can
  always be reflashed by power-cycling. The driver does not say that:

  - `reset()` in `esp_io_expander_ch422g.c` **writes** `REG_WR_IO_DEFAULT_VAL`
    (`0xFF`) and `REG_WR_SET_DEFAULT_VAL` (`0x01`). That runs from
    `esp_io_expander_new_i2c_ch422g()`, which is the first successful I²C
    transaction, not a readout of the pin at power-on.
  - `begin()` in `esp_expander_ch422g.hpp` documents the same action: it *sets*
    IO0–IO7 to output high.
  - `USB_SEL` is **IO5** (§3.4), so **after that write** the mux selects CAN until
    N19 drives it low.

  The silicon level *before* any write has not been measured. Phase 1 measures IO5
  with the ESP32 held in reset. Calling the three notes above "the board powers up
  in CAN mode" conflates a value the driver stores with a pin nobody has probed.

  The path that actually programmed this board is not that mux. §3.1.1: the USB-C
  port enumerates as a CH9102 on GPIO 43/44, ROM download connected immediately,
  and no native USB device (`VID 0x303A`) appeared. GPIO 19/20 are the S3's native
  USB pins, and they are what the FSUSB42UMX steers. Whether `USB_SEL` also
  disconnects the CH9102 is **Q22**. Until that is answered, "CAN on means the
  board cannot be flashed" is the README's claim, not a measurement. What is
  measured is that the mechanical switch can silence the CH9102 completely (D50).
  N19 still runs, so a later `begin()` cannot leave the mux in CAN mode by accident.
  The hardware strap (Q17) remains the only escape that survives a firmware bug
  *if* Q22 finds that `USB_SEL` gates the CH9102.
- **R2 — OTA over WiFi is the only in-service update path.** ArduinoOTA (`:3232`)
  and the `/update` HTTP endpoint are the production path; `esp32usb` is a
  recovery tool used at the bench with CAN disabled.
- **R3 — `Serial` must be UART0, not `HWCDC`.** The template's serial logging has
  to survive `USB_SEL = HIGH`, which it does on the UART0 pins (43/44) but not on
  native USB. Verify which one the module initializes; if it is `HWCDC`, the
  template needs a board-conditional.

The recovery path that has already worked on this board (§3.1.1): put the
mechanical switch in the ESP32 position, hold BOOT, power-cycle, flash over the
CH9102. `USB_SEL` is not low at power-on in any source we have, and it does not
need to be for that procedure. Getting back to CAN is N19's later step — config
says enabled, then `USB_SEL` high and `twai_start()` — not a reset.

**Debugging consequence.** This is not merely an inconvenience. On this board the
device loses its serial console at the exact moment it becomes interesting — the
moment CAN frames start arriving. Every diagnostic that matters therefore has to be
readable **on the panel itself**, which is why §7.6 and §8.12 exist and why N10
makes "no dependence on `Serial`" a hard rule rather than a nicety.

### 3.4 CH422G IO Expander

| Signal | CH422G pin | Meaning | Level that works |
| ------ | ----------- | ------- | ---------------- |
| TP_RST | **IO1** | GT911 reset | HIGH to release from reset |
| LCD_BL | **IO2** | backlight enable → AP3032 EN | HIGH = lit |
| LCD_RST | **IO3** | ST7262 reset | HIGH = released |
| SD_CS | **IO4** | SD card chip select | **LOW** = card selected |
| USB_SEL | **IO5** | USB ⇄ CAN mux | HIGH = CAN, LOW = USB |
| — | IO0, IO6, IO7 | unused | left at the base pattern |
| — | OC0–OC3 (pins 8–11) | open-drain control | **push-pull** for SD_CS |

Three independent sources agree on this map, and the correction is not cosmetic —
writing bit 4 believing it is `USB_SEL` clears `SD_CS` instead and leaves the device
in CAN mode with the card selected:

1. `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/source/waveshare_lcd_port.h:14-18`
2. `.../waveshare_twai_port.h:12-16` and `.../waveshare_sd_card.h:6-10`
3. Espressif's own pin table in `.../esp_expander_ch422g.hpp`, which documents
   IO0–IO7 and OC0–OC3 as expander pins 0–7 and 8–11

The board README's prose table in `boards/.../README.md` is off by one against all
three, including against its own register example further down the same file.

Two properties make a naive "GPIO-like" abstraction wrong:

1. **The SD card CS is active-low on the expander**, so "SD enabled" is written as
   a *cleared* bit. Code that mirrors a GPIO mental model gets this inverted and
   the card is simply never seen.
2. **Several signals are latched by the expander's output-enable and mode
   registers.** The Waveshare examples write a mode byte to one register and a
   pattern byte to another. `tpl_waveshare_lcd7` will own that byte layout behind
   named setters (§6.5) rather than exporting raw register pokes, so no project can
   corrupt a neighbouring bit.

### 3.5 Expansion Headers — J8 is not I²C

The board carries four PH2.0 connectors along one edge, plus two 3-pin headers.
Only one of them can host an I²C sensor, and it is not the one its silkscreen
suggests.

| Ref | Silkscreen | Pins | Carries | Usable here |
| --- | ---------- | ---- | ------- | ----------- |
| **J8** | `Sensor AD` / `3V3 GND AD` | 3 | 3V3, GND, **`AD`** — one analog input on GPIO6 | **No.** Analog only. |
| J6 | `CAN` / `H L` | 2 | CAN H / CAN L | No — the on-board transceiver |
| **H1** | `I2C` / `VCC GND SDA SCL` | 4 | `I2C_VCC`, GND, **`D_SDA` = GPIO8**, **`D_SCL` = GPIO9** | **Yes** — the climate sensor port |
| J7 | `RS-485` / `A B` | 2 | RS485 A / B | Out of scope (§2.2) |
| H2 | `I2C` / `3V3 5V` | 3 | `3V3`, `I2C_VCC`, `5V` | Jumper, not a port |

Three consequences, and the first one is a trap:

1. **J8 cannot carry an AHT20 or a BMP280.** Its third pin is net-named `AD` and
   the schematic block is titled *"Sensor AD"*. It is a single analog line into
   GPIO6. Both of the intended sensors are I²C parts and need SDA *and* SCL, so
   they go on **H1**. A design that names "the sensor port" without naming the
   reference designator will be built wrong at the bench.
2. **H2 selects the I²C bus voltage** and is annotated 默认贴 3V3 — 3V3 fitted by
   default. Both sensors are 3.3 V-only parts, so **the jumper must stay where it
   ships**. It is a soldered link, not a user setting; the design does not depend
   on it being moved, but phase 1 should confirm it (Q13).
3. **H1 is the same bus as everything else on the board.** `D_SDA`/`D_SCL` carry
   the CH422G (`0x24`) and the GT911 (`0x5D`) already, and the touch controller
   is polled from the LVGL read callback (§6.4). Adding two more devices makes
   the I²C bus the most contended peripheral in the design — which is why N13
   exists and why §7.8 specifies a bus-wide mutex rather than reusing §6.5's
   narrower expander one.

### 3.6 The PWM output is not an I²C concern

The heater and pump duty arrives on CAN, not on H1, and `0x240` is a
*bidirectional* frame: `CAN_ID_PWM_SET` is the command sent **to**
`AVR/01_heizung_temp`, `CAN_ID_PWM_REPORT` (`0x280`) is what the controller
reports back. The panel writes the former and renders the latter. §7.9 covers
the write path; the electrical meaning of the duty is the AVR's business, and
the panel treats the value as an opaque 0…1000 permille number.

## 4. Requirements

### 4.1 Functional

| ID | Requirement |
| -- | ----------- |
| F1 | Display a home screen on the 7" panel in **portrait** orientation (480 × 800 logical). |
| F2 | Large clock, **HH:MM only**, no seconds, from NTP. |
| F3 | Stylized house-heater graphic showing three live temperatures: **chamber**, **water in**, **water out**. |
| F4 | Show current power consumption, sourced from CAN. |
| F5 | Five tappable zones opening a detail screen each: chamber, water in, water out, power, and the living-room climate reading. |
| F6 | A gear icon in the bottom-right corner opening the CAN monitor feature. |
| F7 | Every detail screen has a working back button. |
| F8 | Every detail screen carries a **history chart** switchable over **1 day / 1 week / 1 month**. |
| F9 | Record history to the SD card on the board. |
| F10 | CAN decoding strictly via `haus_can.h`; no locally invented IDs. |
| F11 | OTA works through the normal template path. |
| F12 | The whole application runs on a developer Mac without flashing hardware. |
| F13 | When USB is unavailable (CAN enabled), diagnostics remain fully accessible **on the panel**: a 1 MB cyclic PSRAM log buffer and a scrollable log screen with follow-at-end. |
| F14 | A temperature's function (chamber / water-in / water-out) is resolved through **sensor ID**, not through CAN rank: `slot → rank → DS18B20 ROM → function`. Rank is a derived, shiftable address; the function belongs to the sensor and travels with it. |
| F15 | Read living-room **temperature, relative humidity and pressure** on H1, on a fixed cadence, without involving the CAN bus. Pressure and a temperature can come from a BMP280 at `0x76`/`0x77`. Humidity cannot come from an AHT20 on this bus: `0x38` is CH422G WR-IO (R30, Q24). |
| F16 | A fifth tappable zone on the home screen for the living-room reading, opening a **detail screen 5** with the shared skeleton: header value, 1 d / 1 w / 1 m range selector, chart, min/mean/max/now readouts and a data-age line. |
| F17 | Record all three living-room quantities into the same fixed-size history record as the heater series, so they chart at all three ranges and survive the same retention policy. |
| F18 | **Adjust the commanded PWM duty** from the chamber detail screen, over the full **0 → 1000 permille** range, where 0 = off and 1000 = full on. The slider is a *request*, not a report: the value shown as in force always comes from `0x280`. |
| F19 | Commit a duty change only through an explicit **confirmation button**. No drag, release, long-press, gesture or timeout may send a `CAN_ID_PWM_SET` frame. |
| F20 | Display the duty's provenance using `PWM_SRC_*`: whether the value in force is **actively commanded** (including by this panel) or merely **restored from EEPROM** with no live command behind it. |
| F21 | Show the result of the command, not just its dispatch: sent / acknowledged by a fresh `0x280` / timed out, and revert the slider to the reported value if no matching report arrives. |
| F22 | A persistent **bottom navigation pane**, smartphone-style, on every screen except the log screen: Home, the five detail screens, and Settings. One tap reaches any screen from anywhere. |
| F23 | Every glyph on the panel is at least **4 mm cap height** (21 px). No screen-level exemption, including the log screen and the CAN monitor. |
| F24 | A **modelled heater water-out temperature** derived from the commanded PWM duty, shown on the panel and permanently labelled as modelled — dashed chart line, `MODELLED` badge, `TEMP_MODELLED` flag in the record, excluded from every derived statistic. The model function is supplied later; until then the zone renders `—`. This is the heater return, not the living-room sensor. |
| F25 | A user-switchable **CAN enable**, in Settings under the gear, that hands USB back when CAN is off. Defaults to **off**. Confirmed by a modal naming the consequence. |
| F26 | History at all three ranges is served without reading the SD card after boot: the 1-day view from an in-RAM day buffer, the week and month views from an in-RAM rollup loaded once at boot. |
| F27 | The storage state is **operational / faulty / absent**, decided by evidence — a failed rollover write, a CRC mismatch, or free space below the floor all mean *faulty*, not *absent* — and a fault is sticky with the time of first failure. |
| F28 | Settings → Storage shows card type, total, used, free, headroom over the floor, last rollover time, newest history file and a per-tier breakdown (§8.15). |

### 4.2 Non-Functional

| ID | Requirement |
| -- | ----------- |
| N1 | **LVGL ≥ 9.2**, matching the version already used by `19_CYD-project` and `26_CYD_CAN_monitor`. 9.2 is the first release with a working `lv_display_set_rotation()`, which the RGB panel needs. |
| N2 | LVGL stays single-threaded (`LV_USE_OS = LV_OS_NONE`), driven from `loop()`, exactly as `26_CYD_CAN_monitor` does. |
| N3 | CAN reception never blocks the LVGL task. |
| N4 | SD writes never block the LVGL task. |
| N5 | House-state code (decoding, formatting, chart maths) is pure C++ with **no Arduino, ESP-IDF or LVGL dependencies**, so it compiles natively on macOS. |
| N6 | Memory budget: **heap** allocations stay under 64 KB (§10.5.3). Static PSRAM (§10.5.1) is sized at compile time and never reallocated — the 1 MB log ring, the decoded background (768 KB, §8.3), the day buffer and the rollup. The LVGL draw buffers stay in **internal** RAM, because the panel's DMA cannot cross the octal-PSRAM boundary. The chart working set is ~24 KB (§9.3), not 6 KB. |
| N7 | The device stays reachable: WiFi/OTA/watchdog behave as in every other project. |
| N8 | Fonts and background images live in PSRAM or flash, not internal RAM. |
| N9 | Rebuilding `platformio.ini` for a board switch requires changing exactly one `-D` flag. |
| N10 | **No code path may depend on a working `Serial`.** The mechanical switch can kill the CH9102, and Q22 may find that CAN mode does too, so every diagnostic must reach the PSRAM ring, and from there one of the three sinks: the WebSocket (`ws://<host>:81`), the log screen, or the SD dump. The ring must never block on a dead UART/CDC handle or on a stalled socket. |
| N11 | Diagnostic logging is always compiled in. The temptation to strip it for "release" is exactly backwards on a board whose only debugging surface is its own display and its own WiFi. |
| N12 | Log volume is bounded at every stage: capped write rate, a 750 ms WS drain with an 8 KB/tick ceiling and an explicit `dropped` marker, and a 5 Hz on-screen rebuild. A log storm must never become a way to make the panel unresponsive. |
| N13 | **One I²C bus, one mutex.** H1 carries the CH422G, the GT911, the AHT20 and the BMP280, and it is touched by the touch read callback, the expander accessors (§6.5), the SD backend and the sensor task. `Wire` is not thread-safe, so every transaction on the bus is serialised by a single bus-wide lock, not by the narrower expander lock §6.5 already needs. Sensor reads must never hold it longer than one measurement. |
| N14 | **An absent or failed sensor is not a failed panel.** A missing AHT20 or BMP280 degrades the fifth zone to `—` and logs a fault; it must not affect the clock, the heater, the CAN monitor, the recorder or the log screen. The panel boots and is fully usable with no sensors fitted. |
| N15 | **The panel is not a safety interlock.** It commands a duty; it does not enforce one. There is no ramping, no minimum on/off pulse, no upper-bound clamp and no dead-man timeout in this project — all four belong to `AVR/01_heizung_temp`, which is the only place they protect every controller from (§7.9, R20). The panel's job is to be honest about that, not to compensate for it. |
| N16 | A duty change is **rate-limited and audited**. Commits are spaced by a minimum interval, every send is logged with the old and new value, and the value in force is re-read from `0x280` rather than assumed from what was sent. |
| N17 | **4 mm is a floor, enforced at build time.** `FONT_BODY` = 21 px is the minimum `lv_font` in the project; a build-time sweep fails if any font is smaller. Font bitmaps are flash-resident (N8), so the whole scale costs flash, not RAM — there is no budget reason to compromise the one dimension that decides whether the panel is usable (§8.1.1). |
| N18 | **Every history query is bounded and allocation-free.** The week and month views read pre-aggregated columns from PSRAM and never touch the SD card; the only boot-time card read is ~100 KB. No chart build, at any range, allocates heap (§7.10). |
| N19 | **`USB_SEL` is driven LOW as the first statement of `setup()`**, unconditionally, before any other initialisation. Raising it is deferred until after display, touch and sensors are up, and only if CAN is enabled in config (§7.12). |
| N20 | **No periodic I²C traffic for cosmetic purposes.** Dimming, animation and any other periodic effect runs from LEDC or from LVGL, never as a timed I²C write. The I²C bus carries state changes and sensor reads, and 100 writes/s for a backlight would put touch jitter on the critical path (§7.13). |
| N21 | Storage queries never touch the card on a render path. Total/used/free are cached and refreshed on rollover, on card insertion, and on demand — not per frame, and not on every status-strip redraw. |

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
layout, plus a `make run` target. `ESP32/Makefile` today has `fmt`, `links` and
`build`; `run` is a new target and has to be added there, not assumed.
CI (or a pre-commit hook) runs a headless smoke test that boots the app, drives
it through each screen via synthetic taps, and fails on a screenshot diff or an
assertion failure. The baseline is the pinned `lvgl@~9.2.0` plus the host
toolchain recorded next to the images. A font or LVGL bump that changes
rasterisation updates the baseline in the same commit. A diff fails the build;
it does not warn.

**Explicit non-goal.** The harness is *not* an emulator of the panel. It does not
model RGB timing, the CH422G, or touch calibration. Those stay verified on
hardware. What it guarantees is that layout, navigation, decoding, chart maths and
state transitions are correct without a flash — which is where UI bugs live.

### 4.4 Should the Screens Live in SPIFFS?

The request is understandable: hardcoded screens mean a firmware build and an OTA
cycle for a typo. It deserves a real answer rather than a reflex, because the
answer is not "yes" and it is not "no" — it is *which part of the screen is
actually data*.

**The spectrum, and what each option actually costs.**

| # | Option | What moves to SPIFFS | Cost | Verdict |
| - | ------ | ------------------- | ---- | ------- |
| 1 | Hardcoded LVGL (today) | nothing | fastest, full control | baseline |
| 2 | **Theme + strings** | colours, fonts, sizes, padding, every user-visible label | ~300 lines of `lv_style_t` applier; reuses ArduinoJson, already a dependency | **Adopted** |
| 3 | Declarative widget tree | layout of *simple* screens: a generic JSON → `lv_obj` builder | a mini LVGL-DSL: an interpreter, a heap budget, an error surface, and a vocabulary that must be constrained or it becomes slower than the C++ it replaced | Partly (§4.4.2) |
| 4 | Full scripting (Lua on-device) | everything | a ~100 KB VM plus its own heap discipline, and the chart maths (N5) would have to be ported too | Rejected |
| 5 | Web page served to a browser | everything | there is no viable browser engine on an S3; the panel would become a dumb framebuffer and lose live binding, the vector heater and the scrubber | Rejected |

**Why the full version cannot work here, specifically.** Three screens carry this
project, and none of them are declarable:

- **§8.4 the heater graphic** is vector widgets whose colour, flame brightness and
  flow dashes are functions of live values. A layout DSL describes *where things
  are*; this screen's whole point is *what they currently say*.
- **§9.4 the envelope chart** is a streaming downsample (§9.3) painted into an
  `lv_canvas` a column at a time. There is no widget that does it.
- **§8.12 the log screen** renders a windowed slice of a 1 MB byte ring at 5 Hz
  with follow-at-end. Also not a widget.

A declarative layer that could express those three would be a reimplementation of
LVGL's draw pipeline. A declarative layer that could not would not be describing
this application. That is the whole argument, and it is why option 3 is adopted
only where it is honest.

**§4.4.1 Adopted — option 2, theme and strings.** One `/ui.json` in SPIFFS:

```json
{
  "version": 1,
  "units": { "temperature": "C", "pressure": "hPa" },
  "strings": {
    "zone.chamber":  "Chamber",
    "zone.water_in": "Water in",
    "zone.water_out":"Water out",
    "zone.power":    "Current power",
    "zone.living":   "Living room"
  },
  "style": {
    "bg": "#0B0D10", "accent_heat": "#FF6A3D",
    "text_primary": "#F2F2F2", "text_dim": "#6A6A6A",
    "warn": "#FFB347", "alert": "#FF5A5A", "ok": "#5AD98A"
  }
}
```

This covers the large majority of what "I want to change the GUI without touching
code" actually means — a mislabelled zone, a wrong unit, a colour that is too
loud in the evening. It is ~8 KB, it needs no interpreter, and a missing or
malformed file falls back to compiled-in defaults (§4.4.3).

`/spiffs/read?file=/ui.json` already exists in `tpl_webserver.cpp:110`, and
`Makefile:15` already symlinks `prj_template/data/*.html` into every project — so
an editing page in `data/uiedit.html` is a small addition, not a new mechanism.

**§4.4.2 Adopted — option 3, but only for the *static* screens.** Screen
*ordering*, *which screens exist*, *titles*, and the layout of genuinely simple
screens (the power screen, a future settings screen) come from the same
`/ui.json`. The five screens that carry real logic stay hardcoded. The rule is
simple and worth stating in the header comment of the file format:

> If a screen needs to react to a value in a way a widget cannot express, it is
> code. SPIFFS chooses *which* screens exist and what they are called; it does
> not choose what they compute.

**§4.4.3 The fallback rule.** The panel must boot on a hardcoded UI when SPIFFS
is empty, corrupt, or holds a `/ui.json` from a future version — the same posture
§8.3 already takes for the background PNG ("the panel must never boot to a black
rectangle"). A bad layout file degrades the panel to defaults and logs a warning;
it never bricks it. This matters more than usual here, because with CAN enabled
there is no USB console to fix a bad file from, and the only recovery is an OTA
push of a corrected SPIFFS image.

**What this explicitly does not buy.** Changing the *heater graphic*, the *chart
type*, the *slider behaviour* or the *navigation model* still needs a build. That
is the trade, stated plainly rather than discovered later.

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
- **Timing** is fixed by the panel: 16 MHz pixel clock, 4/8/8 horizontal and
  4/8/8 vertical porches, `hsync_idle_low`/`vsync_idle_low` off (active high),
  `de_idle_high` off, pixel clock idle high. These go in as constants in the
  header rather than magic numbers at the call site.
- **`RGB_BOUNCE_LINES = 10`** and `RGB_TRAINING_LINES = 16` are kept from the
  Waveshare reference; the latter matters because the first lines after a reset
  are not valid on this panel.

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
   lock. The expander, the GT911, the BMP280 and the SD chip-select all share
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
board_build.flash_size = 16MB          ; measured, §3.1.1 — the part is 16 MB, not 8 MB
board_build.arduino.memory_type = qio_opi   ; quad flash (Winbond 4018) + octal PSRAM. Without this the core assumes qio_qspi and PSRAM never comes up.
board_build.partitions = custom_16m_ota.csv   ; see the note under partitions — 2x3 MB does not use the extra 8 MB
platform_packages =
build_flags =
    -I include
    -D LV_LVGL_H_INCLUDE_SIMPLE
    -D LV_CONF_INCLUDE_SIMPLE
    -D CORE_DEBUG_LEVEL=1
    -D HOSTNAME=\"${common.hostname}\"
    -D NET_WATCHDOG=\"192.168.1.1\"
    -D CONFIG_SPIRAM_CACHE_WORKAROUND
    -D ARDUINO_USB_CDC_ON_BOOT=0     ; Serial is UART0, GPIO 43/44, the CH9102. §3.1.1, Q2.
    -D ARDUINO_USB_MODE=0            ; do not bring up USB-OTG CDC. GPIO 19/20 belong to TWAI.
lib_deps =
    lvgl/lvgl@~9.2.0
    549@^2.2.1
    bblanchon/ArduinoJson@^6.17.2
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
monitor_speed = 115200

[env:esp32usb]
build_flags = ${env:esp32ota.build_flags}
upload_protocol = esptool
upload_speed = 921600
monitor_speed = 115200
; Reachable over the CH9102 (GPIO 43/44) when the mechanical switch is in the
; ESP32 position (§3.1.1, D50). Whether USB_SEL also has to be low is Q22.
```

Points worth flagging:

- **`partitions`.** `min_spiffs.csv` (the repo default) leaves 192 KB of SPIFFS. A
  project shipping a background image and several fonts wants headroom. Proposed
  custom table: `nvs` 20 KB, `otadata` 8 KB, two 3 MB `app` slots, 512 KB SPIFFS.
  OTA is a stated requirement (F11), so the two-app-slot layout is non-negotiable;
  the sizes are the tunable part. The board has **16 MB** of flash (§3.1.1), so the
  table is `custom_16m_ota.csv`. Two 3 MB app slots plus 512 KB SPIFFS plus the
  small NVS/otadata regions use about 6.5 MB of a 16 MB part and leave about
  9 MB unallocated. Those 3 MB slots are the 8 MB-era sizes. Phase 4 should
  spend the spare on larger app slots (a decoded-asset-free firmware still has
  to carry LVGL and the fonts), and the table needs a `phy_init` partition: the
  image on the board today has 4 KB at `0xf000`, and WiFi calibration looks for
  it. The layout on the board today is otherwise a single 5 MB `factory` app
  with no OTA slot (`rtc_pcf85063`, ESP-IDF v5.5.4). **Erase before the first
  flash.**
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

**The framework choice is a prerequisite, not a detail — and it is bigger than the
expander.** None of this board's driver stack is part of ESP-IDF core. All of it is
*managed components* from the ESP Component Registry:

| Needed by | Component | Minimum IDF |
| --------- | --------- | ----------- |
| §6.5 expander | `espressif/esp_io_expander` | v5.0 on the legacy `driver/i2c.h` API; **v5.2+** for v1.x, which moved to `esp_driver_i2c` |
| §6.3 RGB panel | `espressif/esp_lcd_rgb_panel` | **v5.2+** (`esp_driver_gpio`) |
| §6.4 touch | `espressif/esp_lcd_touch_gt911` | v5.2+ |
| Waveshare port headers | `espressif/esp_display_panel` | v5.2+ |

The vendored Waveshare CH422G driver in `boards/.../source/` is version **0.1.0**
(¤2024) and uses the **legacy** `driver/i2c.h` calls — `i2c_master_write_to_device()`,
`i2c_param_config()`, `i2c_driver_install()` — which exist throughout IDF v5.x and
were removed in 6.0. The C++ wrapper `esp_expander_ch422g.hpp` is ¤2024–2025.

**So the binding constraint is `esp_lcd_rgb_panel` at IDF v5.2+, not the expander.**
Against PlatformIO's `espressif32 @ 6.13.0`:

| Option | IDF | `esp_io_expander` | `esp_lcd_rgb_panel` | Cost |
| ------ | --- | ---------------- | ------------------- | ---- |
| `framework = arduino`, default core 2.0.17 | 4.4.4 | no | no | **does not work** |
| Arduino core 3.0–3.1 | 5.1 | yes (legacy API) | **no** | no RGB panel driver |
| Arduino core 3.3+ | 5.3 | yes (v1.x) | yes | needs a `platform_packages` override — §7.1 already carries an empty one |
| **`framework = esp-idf`** | 5.3+ | yes | yes | the `tpl_*` template is Arduino-based and needs an audit |

Recommendation: **Arduino core 3.3+ with the component manager**, because it keeps
the Arduino-based template intact and clears v5.2. `framework = esp-idf` is the
fallback if that override proves painful. Either way this must be settled in phase
0–1, because §6.3 cannot start without it and the board README's own snippet
already says `framework = esp-idf`. **Q21.**

`lvgl@~9.2.0` rather than `^9.2.0`: N1, D2 and R4 depend on 9.2's rotation
behaviour, and `^` would admit 9.3. `ArduinoJson@^6.17.2` matches
`26_CYD_CAN_monitor` and the copy `tpl_websocket.h` includes. v7 is a different
API; taking it in one project that shares the template headers is a repo-wide
decision, not a silent bump.

`memory_type = qio_opi` is required on whichever core Q21 picks. The bench
measured quad flash and octal PSRAM (§3.1.1); without the flag the Arduino core
assumes `qio_qspi` and PSRAM does not initialise, which silently removes the
background buffer, the log ring and the history tiers. `CONFIG_SPIRAM_CACHE_WORKAROUND`
is the flag `AGENTS.md` and project 26 already set. Confirm it still exists in
the IDF that Q21 lands — a removed Kconfig name would make `-Werror` irrelevant
but a wrong memory type would not. The USB flags above are how Q2 is answered
at compile time (§6.2).

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
- **TX — housekeeping:** one `0x555` frame per second carrying the wall clock,
  which the existing `TIME-BCAST` consumer already understands. No separate
  heartbeat frame: `haus_can.h:64` says `0x555` is already sent once a second by
  every ESP32 on the bus, and a second per-second frame from a new node buys
  nothing a `0x300` stats frame does not already carry.
- **TX — actuator:** `CAN_ID_PWM_SET` (`0x240`) on demand only, from the chamber
  detail screen, behind a confirmation (§7.9). This is the *only* other frame the
  panel ever transmits. There is no ACK to send, no other node to address, and no
  periodic re-send: the duty is durable state on the AVR, not a lease, so
  repeating it would be noise on a shared bus.

The write path has one hard rule: **the panel transmits, and only ever reports
what came back.** A duty it just sent is shown as *requested*, not *in force*,
until a fresh `0x280` carries it (F18, F21). Conflating the two would put a
number on the wall that no actuator is honouring — the same lie §7.4 rejects for
a stale kilowatt figure.

**Mux ordering.** CAN availability is not a compile-time constant:

```cpp
tpl_ws7_setup(TPL_WS7_ROTATION);   // N19: USB_SEL low before anything else
/* display, touch, sensors */
if (config.can_enabled) {
    tpl_ws7_set_usb_mode(false);   // IO5 HIGH -> GPIO 19/20 on the transceiver
    /* twai_driver_install() / twai_start() */
}
```

GPIO 19/20 are free for TWAI because the build never starts a USB peripheral on
them (§7.1). There is no runtime remux call. After `IO5` goes high, whether the
CH9102 console survives is Q22; the on-panel log does not depend on the answer
(N10).

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
  uint32_t epoch;          // minute-aligned unix time
  int16_t  chamber_c100;   // 0.01 C   \ CAN, resolved by function (§7.7)
  int16_t  water_in_c100;  // 0.01 C   /
  int16_t  water_out_c100; // 0.01 C   /
  int16_t  living_c100;    // 0.01 C     local I2C, §7.8 — never empty-slot
  uint16_t hum_c100;       // 0.01 %RH   /
  uint32_t pressure_pa;    // Pa         /
  uint16_t power_dw;      // decawatts (0.1 W). SML watts × 10, saturated at 6553.5 W
  uint8_t  flags;          // VALID bits + FAULT bits, incl. climate-present + TEMP_MODELLED
};
// 4 + 2*4 + 2 + 4 + 2 + 1 = 21, padded to 24 for 4-byte alignment.
static_assert(sizeof(Sample) == 24, "fixed-size records enable seeking");

struct Recorder {
  virtual bool   open(const char *path) = 0;
  virtual bool   append(const Sample &s) = 0;
  virtual bool   flush() = 0;
  virtual size_t count() const = 0;
  virtual bool   read(size_t first, size_t count, Sample *out) = 0;  // random access
  virtual void   close() = 0;
};
```

**Sentinels differ by source, and that is deliberate.** The three CAN
temperatures use `HAUS_CAN_TEMP_EMPTY` (`INT16_MIN`) for "no sensor in this
slot", because that is what the wire says (§7.7). The three climate fields have
no such wire convention — a missing AHT20 or BMP280 is a *device* fault, not an
empty slot — so they use a `CLIMATE_VALID` bit in `flags` and a documented
sentinel of their own. Reusing `INT16_MIN` for a locally-read sensor would make
"the probe is unplugged" and "the chip is not responding" the same value on the
chart, which is exactly the conflation R5 exists to prevent on the CAN side.

- **ESP32 backend:** SD through the CH422G-controlled CS (`tpl_ws7_sd_select` /
  `tpl_ws7_sd_bus`). The day's samples accumulate in the L1 buffer and the file
  is written **once**, at midnight (§7.10), not flushed every minute. A read
  waits until that write has closed the file.
- **Host backend:** identical semantics over a `std::vector` or temp file. This is
  what makes the chart path testable (§4.3).
- **Cadence:** one sample per minute, on the minute, from a dedicated task fed by
  `haus_state` snapshots — unaffected by UI stalls (N4). The climate task (§7.8)
  runs faster than this and simply lets the recorder take the newest value.
- **Never block LVGL.** `flush()` is the only long card operation and runs on the
  recorder task. The card is not held open across a webserver read; reads are gated
  on the recorder having flushed and released (§6.7).
- **Fixed-size records are the key decision.** With a 24-byte record, reading the
  last 1440 samples is `seek(-1440*24, SEEK_END)` — no parsing, no scan, no index
  to maintain or corrupt. §9 depends on this, and every byte figure in §9 is
  derived from 24.

**Power source for F4 — SML, and the decoder already exists.** The source is
`AVR/07_stromzaehler`, and **Q1 is answered**: household power comes from the SML
stream, not from the heater's ADC window. Two findings make that cheap rather than
a project of its own.

**The decoder is already written.** `ESP32/15_power_log_eink` is the current reader
of this stream, and it does not merely reassemble it — it decodes it semantically,
using `libsml` (`sml/sml_file.h`, `sml_crc16.h`). Its `sml_publish()` walks
`SML_MESSAGE_GET_LIST_RESPONSE` entries, formats each OBIS identifier, and matches
on three of them (`ESP32/15_power_log_eink/src/main.cpp:187-247`):

| OBIS | Meaning | Use |
| ---- | ------- | --- |
| `1-0:16.7.0/255` | Stromeffektivwert, in W | **the household power — this is F4** |
| `1-0:1.8.0/255` | total consumption | daily/monthly energy deltas |
| `1-0:2.8.0/255` | total production | export, if ever wanted |

So rev 1.2's claim that `haus_can.h` "has no semantic decoder … needs either a small
SML `1P.1.0` parser or a protocol addition" was wrong in the useful direction: the
parser exists, in a sibling project, and **no protocol change is needed**. What
changes is the work: lift `sml_publish()` and the OBIS matching out of 15's
`main.cpp` into `src/app/` (pure C++, N5), add `libsml` to `lib_deps`, and have
**project 15 adopt the shared version** so the two cannot drift (R26).

**The transport is bursty, which is what the memory map has to absorb.**
`AVR/07_stromzaehler/src/stromzaehler.cpp:71` sends **one CAN frame per 10 ms**, and
a full SML message is up to 258 bytes — so roughly **33 frames, delivered as a burst
over ~330 ms**. Separately it emits a DLC-0 "alive" frame **every second**,
alternating the base ID between `0x600` and `0x700` (`:41-52`). At 125 kbps a burst
occupies well under 1 % of the bus, but the *arrival pattern* is lumpy: 33 frames in
a third of a second, then quiet. Consequences:

- Reassembly uses a **3-deep pool of 258-byte buffers** with a lock flag, exactly as
  project 15 does (`sml_buffers[3]`). That is 774 B and is in the internal-SRAM
  budget (§10.5.2).
- Frames lost mid-message are counted, not silently absorbed — project 15 already
  keeps `sml_lost_bytes_cnt` / `sml_error_cnt` / `sml_empty_message_cnt`, and those
  three counters are carried over verbatim so a bad bus is *visible* rather than
  showing a stale wattage.
- Decoding happens on the **CAN task**, on message completion, so the expensive
  `libsml` parse never sits in the RX drain loop where it would delay the next
  frame.
- The power value is **sampled, not streamed**: L1 keeps the per-minute value
  (§7.10), so a burst that arrives every second costs one assignment per second
  and nothing more.

**The `0x2C0` fallback stays, but with no unit.** `AVR/01_heizung_temp/README.md`
derives a current in amperes via a documented chain (`÷10` turns ratio, `×30 A/V`,
against a 0.319 A pump rating), and that chain was derived from a resistor network
since changed to 1k/1k/1k with an 18k leg. The AVR samples on the internal 1.1 V
bandgap (`src/adc_monitor.cpp:42`) and publishes raw counts, so the numbers on the
wire are valid; what they mean in amps is not. It is therefore a **unitless activity
indicator** — useful for "is the pump running", useless as a watt figure — and it
is never the primary source. Q14 stays open on whether to show it at all.

Recommended: SML primary with an `SML` badge, `0x2C0` as a dimmed unitless activity
bar, and an explicit "—" when SML has not produced a value. A wall panel showing a
stale kilowatt figure is a lie with a fresh-looking timestamp; so is a plausible amp
figure with a scale factor that no longer matches the hardware.

### 7.5 OTA and firmware-update paths

`USB_SEL` high steers GPIO 19/20 to the CAN transceiver (§3.3). Whether that also
kills the CH9102 console is Q22. OTA does not depend on the answer; USB flashing
might.

| Path | Mechanism | When used |
| ---- | --------- | --------- |
| **ArduinoOTA** (primary) | `:3232` over mDNS via `tpl_wifi_setup` | Production. Unaffected by CAN. |
| **HTTP `/update`** | `tpl_webserver` POST handler | Alternate production path; also unaffected. |
| **USB flash** (recovery) | `esp32usb` env, `esptool` | Bench only, with CAN disabled. |

Requirements that follow:

- **Check the mechanical switch before anything else.** A silent board is a switch
  in the wrong position far more often than it is a dead board (§3.1.1). The port
  will enumerate and open cleanly and still return nothing, at any baud, with any
  DTR/RTS sequence. Symptom: `esptool` reports `No serial data received`. This
  costs five seconds to rule out and is the first line of the runbook.
- **`Serial` is UART0.** The connector is a CH9102 on GPIO 43/44 (§3.1.1), and
  §7.1 pins `ARDUINO_USB_CDC_ON_BOOT=0`. Phase 1 confirms the firmware's `Serial`
  object matches that (Q2, resolved). R3's remaining content is the flush budget,
  not the console.
- **Watchdog restarts re-run N19.** `setup()` drives `USB_SEL` low before it
  reads config, then raises it again if CAN is enabled (§7.12). A restart does
  not inherit a pin state from the previous boot. The bad case is a crash *after*
  that raise, looping there. Whether the CH9102 is gone in that window is Q22.
  Crashes are logged to the SD dump from §7.6, which does not need the console.
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
reading a stack trace by squinting at a 21 px monospace font on a wall panel is
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

### 7.8 Local I²C Sensors — Living Room

An AHT20 (temperature + relative humidity) and a BMP280 (temperature + pressure)
sit on the **H1** header (§3.5), read directly by the panel.

**These are not CAN sensors and must not be routed through §7.7.** That whole
apparatus exists because a DS18B20's identity travels on the bus at a rank that
shifts. An I²C device has a fixed address soldered into the part, is physically
where it is, and is configured by whoever plugged it in. There is no rank, no
identity frame, no expiry, and no `sensor_roles[]` entry. Forcing it through the
CAN resolution chain would add a failure mode (an unbound sensor rendering `—`
forever) to a sensor that cannot have that failure. F15's data enters
`HausState` directly, already function-identified by virtue of *being* that
function.

**Devices on the bus.** All four share GPIO 8/9:

| Device | Address (7-bit) | Bus role |
| ------ | --------------- | -------- |
| CH422G WR-OC | `0x23` | open-drain / push-pull for OC0–OC3 |
| CH422G WR-SET | `0x24` | direction and mode. This is the address §3.1 used to quote alone |
| CH422G RD-IO | `0x26` | input readback |
| CH422G WR-IO | `0x38` | the output pattern. **Same address as an AHT20** |
| GT911 touch | `0x5D` (or `0x14`) | polled from the LVGL read callback (§6.4) |
| **BMP280** | `0x76` if SDO is low, `0x77` if SDO is high. **Not `0x40`** | read by the sensor task. Neither address collides |
| **AHT20** | `0x38` | **collides with WR-IO. Do not fit one on this bus** |

Rev 1.4 said "no address conflict" and gave the BMP280 as `0x40`. Both are wrong.
`0x40` is not a BMP280 address (the part only answers `0x76` or `0x77`). `0x38`
is the AHT20's only address and it is also the address `i2c_master_write_to_device`
uses for every CH422G output update (`CH422G_REG_WR_IO = 0x70 >> 1`). An AHT20 on
H1 receives every backlight, reset, chip-select and USB-mux write as a command,
and an AHT20 read can be accepted by the expander as a new output pattern. That
breaks the mux and the sensor together. **R30, Q24.** Until Q24 picks a part
whose address is outside `{0x23, 0x24, 0x26, 0x38, 0x5D, 0x14}`, the living-room
zone stays `—` (N14). A BMP280 does not have this problem.

Contention is still real for the devices that do share the bus. The touch poll
runs on `loopTask` and can land between two steps of a BMP280 register burst, and
Arduino's `Wire` has no re-entrancy protection. Hence **N13** — one bus-wide
mutex, which is the same lock §6.5 takes, and the sensor task holds it for one
measurement at a time.

**Per-device requirements.**

- **AHT20** — only if Q24 finds a way to put one on a bus that is not this one.
  On its own bus: read the status byte first and treat bit 7 as busy; a
  measurement is ~80 ms, so the task polls rather than sleeping through it. The
  6 data bytes carry a CRC-8 (poly `0x31`, init `0xFF`) that must be checked. A
  noisy bus returns plausible wrong numbers more often than an outright NACK.
  The polynomial is the part's; it is not programmable and it is not "known-bad".
  Power-on needs ≥40 ms before the first command and a soft reset with ≥20 ms
  after it.
- **BMP280** — forced mode only. The chip id at `0xD0` must read `0x58`. The
  calibration block is 24 bytes at **`0x88`** (`dig_T1` through `dig_P9`,
  registers `0x88`–`0x9F`), read before any compensated value is used. `0xE4` is
  not that block — humidity calibration on a BME280 starts at `0xE1`, and this
  part is a BMP280. Forcing a fresh measurement each cycle keeps the part asleep
  between reads.

**Cadence and degradation.** The sensor task samples every 30 s and writes into
`HausState`; the recorder samples once a minute (§7.4), so the extra resolution is
for the live display and the log, not for the history. Per N14 a missing or
crc-failing device sets its `CLIMATE_VALID` bit clear, raises a fault flag and
logs — and nothing else on the panel changes. Three consecutive failures log once
at `!`, not three times.

**What the panel does not do with them.** It does not reconfigure oversampling,
filter or IIR coefficients (§2.2), and it does not try to cross-check the AHT20's
temperature against the BMP280's. They will disagree by a few tenths in a real
room; showing both would be noise, so the AHT20 is authoritative for temperature
and the BMP280 is used for pressure only.

### 7.9 Actuator Control — PWM Duty on `0x240`

The panel is the controller of record for the commanded duty. It writes
`CAN_ID_PWM_SET` (`0x240`) and renders `CAN_ID_PWM_REPORT` (`0x280`).

**One channel, two devices.** `haus_can.h` defines no separate pump identifier.
The heater element and the circulation pump are driven from the single
bidirectional duty output, so one slider commands both and **this project needs no
change to `haus_can.h` at all** — which is the cheapest possible outcome and the
reason heater and pump were settled as one control point rather than two.

**The encoding, from the firmware rather than from assumption.**

| Property | Value | Source |
| -------- | ----- | ------ |
| Field | `duty_permille`, `uint16_t` LE, bytes 0–1 | `haus_can.h:219-221` |
| Direction | **0 = off, 1000 = full on** — it is *high time* | `pwm_out.cpp:124-125`, `pwm_out.h:119`, `haus_can.h:254` |
| Endpoints | 0 and 1000 are held by the port register, not modulated | `pwm_out.cpp:121-131` |
| Interior | clamped to 10…990 permille by the output stage | `pwm_out.cpp:138-139` |
| Reported | `frequency_hz` bytes 2–3, `flags` byte 4 | `haus_can.h:222-223` |
| Source | `flags` bits 1–2 = `PWM_SRC_EEPROM` / `_CAN` / `_NRF` / `_FAULT` | `haus_can.h:243-247` |

Because it is high time, **the slider runs left = off, right = full**, and 0 is
"off" — not 1000. A control labelled the other way round would command full power
at the position a person reads as "off", which is the single most consequential
thing to get right in this section.

**The four things the panel does not do** (N15, restating `main.cpp:523-525`,
which says the node applies *"no ramping, no minimum-on/off pulse, no other
validation than the permille clamp inside `pwm_out_set()`"*):

1. **No ramp.** A change is one step, immediately.
2. **No upper clamp.** `pwm_out_set()` accepts anything up to 1000
   (`pwm_out.cpp:106`) and that is the only bound in the system. The panel's
   slider offers the full range because that is what was asked for, and it logs
   every commit — but see R20.
3. **No dead-man timeout, and it cannot have one.** The duty is durable state by
   design (`main.cpp:140-152`): *"a duty can outlive its author… a controller
   merely rebooted is a far more common event than a controller dying."* If the
   panel sent a heartbeat and stopped, the heat would drop on every panel
   restart. The panel therefore does not retry, and stopping the panel does not
   change the duty.
4. **No minimum on/off pulse**, so nothing prevents a duty that is too low to move
   the pump.

**The duty survives a power cut.** It is persisted in the AVR's EEPROM restore
point and reported on `0x2C8` (`haus_can.h:388`). A command sent from the wall is
therefore *not* undone by a power failure, and comes back at the next boot. The
UI says so at the moment of commit (§8.6) rather than leaving it to be discovered
after an outage.

**Provenance is the panel's main reporting job here.** `PWM_SRC_*` distinguishes a
duty that is actively commanded from one merely restored from EEPROM. That is the
difference between "the house is holding temperature because something is
controlling it" and "the house is holding temperature because of a value that was
written before the last reboot and nobody has spoken since" — and `haus_can.h:242`
goes out of its way to make it visible. F20 exists for it: a restored duty gets a
distinct badge, not the same green as a live one.

**Write path.**

- **Only** from the confirmation button (F19). No gesture, drag-release, timeout
  or long-press may transmit.
- Rate-limited per N16, with a minimum interval between commits.
- Every send logs the old and new permille value, and the log line carries the
  `PWM_SRC_CAN` expectation so a reader can tell a panel commit from an nRF one.
- The value shown as in force always comes from `0x280`. A commit that is not
  echoed by a fresh report within a timeout shows as **failed** and the slider
  reverts to the reported value (F21).
- TX failures (`TWAI_ALERT_TX_FAILED`) set the same fault flag as a bus error, so
  a command that never left the panel is visibly different from one the AVR
  ignored.

### 7.10 Three-Tier History Store

The history subsystem is three tiers with different resolutions and different
write policies. The tier a query lands in is decided by the range, and no query
ever scans a sample file.

| Tier | Granularity | Where | Size | Answers |
| ---- | ----------- | ----- | ---- | ------- |
| **L0** samples | 1 min | SD, one file per day | 34 560 B/day | nothing directly — the durable record |
| **L1** day buffer | 1 min | PSRAM, today only | 34 560 B | **1 day**, at full resolution |
| **L2** rollup | 1 hour, 31 days | PSRAM, loaded at boot | 65 472 B | **1 week** and **1 month** |

**L2 column layout** — one column per hour, 744 of them:

```cpp
// src/app/rollup.h
struct RollupColumn {          // 4 + 24 + 24 + 24 + 12 = 88 B, no padding
  uint32_t t;                  // bucket start, unix minutes
  int32_t  mn[6];              // 24 B
  int32_t  mx[6];              // 24 B
  int32_t  sum[6];             // 24 B
  uint16_t n[6];               // 12 B
};
```

Six series — chamber, water-in, water-out, living room, humidity, pressure —
because `int32_t` min/max/sum is what makes this 88 B rather than the 60 B a
`int16_t` layout would give, and pressure in pascals does not fit in an `int16_t`
at all. The struct is written and read as a flat block with no per-field
endianness concerns beyond the usual little-endian assumption both the ESP32 and
the host harness share (N5).

**Why the week and month views do not touch the SD card.** A month is 43 200
samples = 1 012 KB (§9.2). Streaming and downsampling that on every range switch
is the 500 ms figure §9.5 used to carry, and it would run on the recorder task
with the UI waiting on it. With L2 in RAM the same answer is a memcpy of 65 KB and
a rescale — about a millisecond. The month view becomes instant, the LRU chart
cache from §9.5 disappears, and the "chart loading" spinner goes away entirely.

The cost is honesty about resolution: at 1 hour buckets the month view shows the
envelope *of hourly aggregates*, not of individual minutes. That is stated in the
chart's own readout rather than left for the reader to assume, and the 1-day view
— which is where minute-level detail actually matters — is served from L1 at full
resolution.

**Boot sequence.**

```
1. mount SD, open /rollup.bin          (65 472 B, ~35 ms)
2. read the newest /data_YYYYMMDD.bin   (34 560 B, ~20 ms)
3. re-apply those samples to L2          -> catches up the columns the rollup predates
4. replay them into a fresh L1 for today
5. ui_init() and first frame
```

Steps 1–4 total ~100 KB and complete **before the first frame is drawn**, so no
chart is ever seen half-populated and there is no spinner on the home screen. If
the card is absent, steps 1–4 are skipped, L1 starts empty and the charts say
"no data" — never an empty axis that reads as "nothing happened" (§9.5).

**Daily rollover**, at local midnight on the recorder task:

1. finalise the last hour column of L2; append it
2. write `/data_YYYYMMDD.bin` — the whole day's 1 440 samples, one write
3. rewrite `/rollup.bin` with the current 744 columns, one write
4. clear L1 and start a new day

Two writes a day, 34 560 + 65 472 = 100 032 B. A card that is only ever written twice a day will
outlast the panel it is in.

**The trade this makes, stated plainly.** Between rollovers, the day's history
exists only in PSRAM. A power cut loses up to 24 hours of minute data — which §9.3
renders as gaps, honestly, rather than as a fabricated line. The alternative,
writing every minute, protects the last minute at the cost of 1 440 writes a day
and an SD card that fails in months. For a wall panel that is not attended, the
second failure is worse than the first, so the day's buffer is the right trade.
It is recorded as **Q16** because it is a judgement, not a derivation.

### 7.11 Modelled Water-Out Temperature

The DS18B20 probes are slow and there is no return-line sensor, so the third
temperature zone is **derived from the commanded PWM duty** rather than measured
(§12 Q9). The model function is not yet known; it is to be measured and supplied
later. Everything here is built around that being true.

**It is a strategy, not a value.**

```cpp
// src/app/water_model.h   — pure C++, no Arduino/ESP-IDF (N5)
struct WaterModel {
  virtual ~WaterModel() = default;
  virtual bool estimate(int16_t water_in_c100,
                        uint16_t duty_permille,
                        int16_t chamber_c100,
                        int16_t *out_c100) const = 0;
  virtual const char *provenance() const = 0;   // "model v1", "sensor", …
};
```

`estimate()` returns false when it has nothing to say — no model, or inputs that
are out of the domain — and the caller then renders `—`. A model that cannot
answer must not answer with a guess.

- **Default implementation** is `nullptr`. Until the function is supplied, the
  third zone behaves exactly as rev 1.2 described: `—` with the reason shown.
  Nothing about the UI, the recorder or the charts changes when it arrives.
- When a model is installed it is registered once at boot and is selectable from
  `/config.ini`, so the measured function can be compared against a placeholder
  side by side.
- **A real sensor always wins.** If a return-line DS18B20 is fitted and bound
  (Q9 option a), resolution comes from §7.7 and the model is bypassed entirely.
  The two paths are not merged.

**Everywhere it appears, it is labelled.** This is the part the document cares
about most, because D20 and R16 exist precisely to stop a plausible number
standing in for a measurement:

| Surface | Treatment |
| ------- | --------- |
| Home screen zone | value in the water-out position, with a small `~` glyph before the number and a dimmed plate behind it |
| Detail screen 3 header | a permanent `MODELLED` badge next to the title, and the model name beneath it (`model v1`, `—`) |
| Chart | **dashed** line and a dashed min–max envelope, against the solid styling every measured series uses. The distinction survives a glance and survives a screenshot |
| Chart readout | min/mean/max carry a `~` prefix |
| History | a dedicated `TEMP_MODELLED` flag bit in `Sample.flags`, so a record written from the model can never be read back as a measurement |
| Log screen | every modelled value written to the ring is tagged `!` amber, not plain `I` info |
| **Efficiency stats** | **excluded.** "Time above target" and any rise/fall figure (§8.6, §8.8) are computed from measured series only — a statistic derived from a model is a statement about the model |

**What it is good for.** The shape. A modelled series makes it obvious *when* the
water heats up after a duty change, which is the question a person actually has
while the real probes lag — and the shape is exactly what the min/mean/max
envelope is designed to show. What it is not good for is a number to compare
against a setpoint, which is why every statistic drops it.

**R24** carries the residual risk, and the mitigation is structural rather than
editorial: the flag bit and the distinct rendering mean a modelled value cannot
silently become a measured one, even if a future contributor forgets which is
which.

### 7.12 The CAN Enable Switch

A user-switchable **CAN on / CAN off** control, in Settings under the gear
(§8.14). Its purpose is the one §3.3 spends a page on: **turning CAN off gives
back USB**, which is the panel's serial console, its flash path and its only
recovery route.

**Default is off.** That is a config default plus N19, not the expander's
power-on level (§3.3). A panel that has never been configured is told to drive
`USB_SEL` low before it reads the config. The CH9102 console on GPIO 43/44 is
what recovery actually uses, subject to the mechanical switch and to Q22.

**The setting is persisted** in `/config.ini` via `TplSpiffsConfig`, so the panel
boots into whichever mode it was left in. The boot sequence is therefore:

```
1. CH422G up; drive USB_SEL LOW immediately, before any other initialisation
2. ... everything else: I2C, display, touch, sensors, WiFi
3. if (config.can_enabled)  vTaskDelay(200 ms);  USB_SEL HIGH;  twai_start()
4. else                     USB_SEL stays LOW;  TWAI not started
```

Step 1 is unconditional and first, which is the only ordering that makes the
default meaningful. Raising `USB_SEL` before the display exists would buy nothing
and would remove the ability to recover from a fault in display bring-up — the
exact class of problem R1 is about.

**What the switch actually changes.** With CAN off: no TWAI driver is started,
`canTask` is not created, every CAN-sourced zone renders `—`, charts show no
data, and the CAN monitor screen is replaced by an explanatory panel. The panel is
otherwise fully alive — clock, sensors, log screen, webserver, OTA. That is a
deliberate demonstration of §7.6: with the bus off, the log ring and the log screen
are the only diagnostics that remain, which is exactly why they were built to
survive on their own.

**Leaving the driver installed is unnecessary complexity.** The FSUSB42UMX
disconnects the transceiver when `USB_SEL` is low, so an installed-but-stopped
TWAI driver cannot interfere. The simpler path is not to start it.

**The recovery gap, named.** If CAN is enabled in config and the firmware then
crashes *after* step 3, `USB_SEL` is high. Whether that removes the CH9102 is
**Q22**, not an unanswered power-on default. The driver's `0xFF` is a value it
writes, and Q15 (a second panel) has nothing to do with it. Three escapes, in
order of preference:

| Option | Mechanism | Robustness |
| ------ | --------- | ---------- |
| **A. Hardware strap** | a jumper on **GPIO 4** to GND forces the firmware to hold `USB_SEL` low. GPIO 4 is outside the RGB/I²C/SD/CAN/UART map (§3.2). GPIO 15 and 16 are also absent from that map; phase 1 confirms they are not inside the module's octal bus before anyone treats 4 as the only choice | **Best against a firmware bug**, and only useful if Q22 says `USB_SEL` gates the CH9102. The template no longer claims GPIO 4 (§6.7). Q19 wants the same pin for LEDC backlight |
| B. Liveness timeout | if CAN is enabled but no frame is seen within N seconds, drop back to USB and log it | Good against a silent bus, useless against a firmware that crashes before the timer runs. Also silently disables the panel's main job, so it should be **off by default** and configurable |
| C. BOOT + power cycle | ROM download over the CH9102, which §3.1.1 already did on this board | Works today, with the mechanical switch in the ESP32 position. It does **not** depend on `USB_SEL` being low unless Q22 finds that the switch and IO5 are the same gate |

Option A is a board modification, so it is **Q17**. It is not available to
firmware alone, and it shares its pin with Q19.

### 7.13 Backlight Dimming — Why Not PWM the Expander

R9 records that the backlight is binary and no PWM exists. The obvious workaround —
chop `LCD_BL` (CH422G IO) at 100 Hz — is worth answering properly, because the
circuit invites it and the answer is no.

**The circuit** (schematic sheet 1, verified against
`boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/ESP32-S3-Touch-LCD-7-Sch.pdf`):

```
U3 AP3032  PFM boost LED driver:  IN(6)  SW(1)  EN(4)  GND(2)  OV(5)  FB(3)
           L6 10uH + D6 IN5819 + C26 10uF + C8 10uF

CH422G IO ── BL_EN ── R52 (0R, FITTED) ── BL_EN_1 ── EN(4)     ← backlight on/off
                       └─ R49 (0R, NC) ─ R50 (10k, NC) ─ R51 (68k, NC) ── FB(3)
                                                       └─ C34 (100nF, NC) ── GND
```

So the expander bit lands on **EN, a logic input**, and the feedback divider is
deliberately unpopulated. EN *is* the dimming input — chopping it does reduce
average LED current. Three things still make it the wrong choice here.

**1. There is no hardware PWM behind an I²C expander.** LEDC drives GPIOs; a
CH422G output is a register bit a microcontroller has to write. 100 Hz therefore
means a **task** issuing 100 I²C writes per second, each ~113 µs, each taking
the bus mutex — on the same bus the GT911 poll and the sensor task contend for
(N13). 1.1 % of bus bandwidth is acceptable; **100 extra mutex acquisitions per
second against the touch poll is not**, because the symptom is touch jitter and a
wall panel with jittery touch is a broken wall panel. It also cannot be done from
a timer ISR, so the task's own jitter lands directly on the duty.

**2. The response is charge-transfer, not proportional.** With EN low the boost
stops and C26 (10 µF) plus the panel's own backlight capacitance keep feeding the
LED string from stored charge. Two consequences: the LED **never reaches zero**,
because there is a brightness floor set by that charge; and brightness-vs-duty is
**convex**, not linear, drifting with panel temperature and the fitted
capacitance. A nominal 20 % duty will not look like 20 % brightness, and the
control would need calibrating against the actual panel to be trustworthy.

**3. 100 Hz sits on the flicker threshold**, and the dimming range *falls* as
frequency rises, because less charge is lost per cycle. So the frequencies that dim
well are the ones that visibly flicker — a direct tension with no good middle.

| Option | Mechanism | Verdict |
| ------ | --------- | ------- |
| **A. Content dimming only** | darken the UI toward the background colour; backlight stays lit | **Adopted.** Free, no flicker, no bus traffic, and it composes with the dark theme already in §8.1. R9's screensaver is this, made explicit |
| B. PWM IO at 100 Hz from a task | 100 I²C writes/s | **Rejected** — bus contention with the touch poll, convex response, flicker |
| C. Chop at 30–60 Hz | wider dimming range | **Rejected** — visible flicker, unacceptable on a wall panel |
| **D. Move EN to a GPIO, use LEDC** | **desolder R52 (0 Ω), wire EN to a free ESP32 GPIO, drive with LEDC** | **Recommended if any hardware change is acceptable.** True hardware PWM: zero CPU, zero I²C, no contention, clean and linear, any frequency |
| E. Dim through the FB divider | populate R49/R50/R51 and inject a PWM into FB | Possible, invasive, load-dependent. Not recommended |

**Option D has a pin conflict.** GPIO 4 is the pin §7.12 wants for the CAN
force-USB strap. It is not claimed by a template LED macro (§6.7), and it is not
in the RGB/I²C/SD/CAN/UART list. GPIO 15 and 16 are also missing from that list;
until phase 1 looks at the module pinout, "the only free GPIO" is a claim this
document does not make. Two mods, one preferred pin: **Q19**. It is a soldering
decision.

If D is taken, three things change: §3.2's "on/off only, no PWM" becomes wrong;
`tpl_ws7_backlight()` gains `tpl_ws7_backlight_pwm(uint8_t percent)` alongside the
boolean; and R9 closes. If D is not taken, **A stands and R9 keeps its current
mitigation** — the panel never dims its backlight, only its content, and says so.

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

#### 8.1.1 The type scale — a physical floor, not a style preference

Font sizes on this panel are specified in **millimetres of cap height**, because
the panel is a physical object mounted on a wall and the only question that
matters is how large a glyph is to the eye standing in front of it.

**The conversion.** The panel is 7″ diagonal (177.8 mm) at 800 × 480, a 5:3
aspect, so the active area is 152.4 × 91.4 mm. That gives **5.25 px/mm — 133 DPI**
in both axes (800 px over 152.4 mm, 480 px over 91.4 mm). So:

> **millimetres × 5.25 = pixels**, and **4 mm body text = 21 px.**

**4 mm is a hard floor.** The stated requirement is readability without glasses,
and 4 mm is the smallest glyph that holds up at arm's length for someone over 40.
It is not negotiable per screen, and §8.10 and §8.12 were resized to obey it rather
than exempted from it.

| Role | mm | px | Used for |
| ---- | -- | -- | -------- |
| `FONT_CLOCK` | 21.0 | 110 | the home-screen clock |
| `FONT_LARGE` | 9.9 | 52 | detail-screen headline values |
| `FONT_VALUE` | 6.1 | 32 | home-screen zone values, chart axis labels |
| **`FONT_BODY`** | **4.0** | **21** | **the floor: all body text, labels, readouts, table rows, log lines** |
| `FONT_MONO` | 4.0 | 21 | log screen, same floor |

**The audit of what rev 1.2 already specified** — two violations, both fixed:

| Was | mm | Verdict |
| --- | -- | ------- |
| §8.12 log screen, 16 px monospace | 3.05 | **below floor** → 21 px |
| §8.10 CAN monitor rows, 20 px Montserrat | 3.81 | **below floor** → 21 px, row height 44 → 50 px |
| §8.2 clock, ~110 px | 21.0 | ok |
| §8.4 heater value labels, 28 px | 5.33 | ok |
| §8.2 status strip band, 28 px | 5.33 | ok |

**What obeying the floor costs, quantified.** This is a real reduction in
information density and it is not free:

| | at 16 px | at 21 px |
| - | --------- | --------- |
| Chars per line (480 px, monospace ≈ 0.6 em) | 50 | **38** |
| Visible log lines (628 px of text area, 1.15 line spacing) | ~34 | **~26** |

Long log records therefore wrap or truncate. That is the accepted consequence of a
readable console, and it is why §8.12 switches the log view to **soft wrapping at
38 characters** rather than clipping — a stack trace cut off at column 38 is
useless, whereas one that wraps is merely taller. The dropped-line counter (§8.12)
matters more than ever, because fewer lines means the 1 MB ring holds less
readable text.

**Enforcement, not just intent.** A `TXT_MIN_PX` compile-time constant is defined
as `FONT_BODY`, and phase 7's exit criterion is a sweep that fails if any
`lv_font` in the project is smaller. LVGL makes this easy: font glyph bitmaps are
flash-resident (§4.2 N8), so the whole scale costs flash, not RAM, and there is no
reason to economise on the one thing that determines whether the panel is usable.

### 8.2 Home screen layout

480 × 800 logical pixels (portrait). Bands, top to bottom:

```
 y=0
 +---------------------------------------------------+  0
 |  Mon 14 Oct                        [CAN ok] [SD] |   status strip, 32 px
 +---------------------------------------------------+  32
 |                                                   |
 |                   08:47                           |   clock, ~140 px tall
 |                                                   |   FONT_CLOCK, 110 px
 +---------------------------------------------------+  172
 |                                                   |
 |        +----------------------+                   |
 |        |                      |  <- flame          |
 |  21.4  |      CHAMBER         |     ~110 px       |
 |   degC |                      |                   |
 |        +-----------+----------+                   |
 |    62.1 degC   ^   48.6 degC                      |   pipe labels, 21 px
 |     water in   ~   water out                      |   (~ = modelled, 7.11)
 |                                                   |
 +---------------------------------------------------+  580
 |  LIVING ROOM            21.8 degC   47 %rh        |   strip, 76 px
 |                                     1013 hPa      |
 +---------------------------------------------------+  656
 |   CURRENT POWER                     1.284 kW     |   one band, 56 px
 +---------------------------------------------------+  712
 |                                                   |
 |  HOME  CHAM  WIN   WOUT  PWR  LIVING      [gear]  |   nav pane, 88 px
 +---------------------------------------------------+  800
```

**The living-room band is a strip, not a fifth graphic.** §8.1's rule is that
the picture carries the data, and the heater graphic does that job for the three
temperatures because they are *of* the heater. A living-room reading is not
physically part of that picture, and inventing a second illustration to carry one
number would dilute the one that works. So it gets a quiet horizontal band: name
on the left, temperature large, humidity and pressure smaller beside it, tappable
across its full width (§8.5). It is the only band on the home screen that is
text-first, and that is the honest treatment — it reads as *a different room*,
which is exactly what it is.

Notes:

- **Clock without seconds** (F2), because the panel is glanced at, not watched.
  The seconds are known to the renderer but not shown; an optional seconds overlay
  exists behind a long-press for debugging, off by default.
- **Date and weekday** share the status strip. Without them "08:47" is ambiguous.
- **Temperatures sit on the graphic**, not in a row beneath it. A number with a
  degree sign next to a pipe reads as a property *of that pipe*, which is the point
  of the stylised rendering.
- **The gear is the last nav item**, bottom-right in the pane (F6). It moves from a
  hidden footer corner to a persistent target, which is a deliberate reversal of
  rev 1.2's reasoning: a pane showing six tabs and hiding the seventh behind
  nothing would be strange, and a persistent target is easier to hit at arm's
  length than a 64 px corner nobody is sure about. It stays the quietest of the
  seven — outline glyph, not filled, no underline unless active.
- **Status strip indicators** are small dots, not text: green CAN, amber CAN fault,
  green SD, red SD fault, grey WiFi-off, and — only when a sensor is actually
  missing — a grey `I²C` dot (N14). A climate sensor that is absent is not an
  error the eye should catch across the room, but it must not be silently
  indistinguishable from a reading either.
- **The duty badge lives in the status strip too.** Once a PWM command has been
  sent, a small marker appears there so that the provenance described in §7.9 is
  visible from the home screen without opening the chamber detail: solid when the
  duty in force is actively commanded, hollow when it is a restored EEPROM value
  (F20).

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

**The ramp is temperature-only, and the other quantities get their own.** A single
"blue → amber → red" scale is meaningful for temperature because everyone already
has an intuition for what 60 °C looks like. It is meaningless for humidity and
pressure, where the same visual would imply a comfort judgement that neither
quantity supports — a red 65 %rh is not "hot" in the way 65 °C is. So:

| Quantity | Ramp | Rationale |
| -------- | ---- | --------- |
| Temperature (all four sources) | blue → amber → red, 30/60 °C breakpoints | shared, so the three heater values and the living-room value are visually comparable |
| Relative humidity | single desaturated cyan, no ramp | a magnitude, not a state; the number carries the meaning |
| Pressure | single dim violet, no ramp | likewise, and it moves on a scale nobody perceives directly |

All three are overridable from `/ui.json` (§4.4.1) — this is exactly the kind of
house-specific styling that belongs in the config file rather than in a rebuild.

**Texture for "fancy".** Three cheap additions that survive an RGB flush budget:
a soft outer glow on the firebox (drawn as three concentric low-alpha rounded rects),
a per-pixel dithered noise overlay in the dark areas (a 4 × 4 tiled 16×16 ARGB
pattern at ~6 % opacity, one `lv_image` tile), and a 1 px inner highlight along the
top edge of the cabinet. Together these read as a rendered scene rather than flat
shapes, and together they cost three cached images, not three per-frame passes.

### 8.5 Touch model: five zones + gear

Zones are **elliptical/rectangular hit regions declared as invisible `lv_obj_t`
button overlays**, not ad-hoc coordinate tests in a read callback. That keeps hit
areas independent of the drawing and gives LVGL's own press/click handling, focus
and scroll-guard behaviour for free.

**With the nav pane (§8.11) the tabs are the primary route to a detail screen and
the zones are the secondary one.** Both work; the zones are kept because the
overlays already exist and because tapping the number you are looking at is the
most direct gesture available. What changes is the design pressure — the zones
no longer have to be discoverable on their own, so they can be sized generously
and overlap the artwork without a mis-tap being costly.

| Zone | Region (portrait coords) | Opens |
| ---- | ------------------------ | ----- |
| Chamber | centre rect over the firebox, ~300 × 180 at y≈250 | Detail screen 1 |
| Water in | pipe segment + label, ~180 × 120 at y≈500 | Detail screen 2 |
| Water out | pipe segment + label, ~180 × 120 at y≈500 | Detail screen 3 |
| Power | full-width band, y=656, 56 px tall (the §8.2 row) | Detail screen 4 |
| **Living room** | full-width band, y=580, 76 px tall | **Detail screen 5** |
| Gear | last tab of the nav pane, y=712, 88 px (§8.11). Not a 64 × 64 overlay | CAN monitor |
| Clock | (optional) whole clock area | toggles °C / °F and a 24 h clock |

The §8.2 drawing places the living-room band at y=580–656 and the power band at
y=656–712, flush with each other and with the nav pane at y=712. An 8 px dead
zone between those two bands would have to come out of the 56 px power row, so
the boundary itself is the guard. The 8 px dead zone stays on the water-in /
water-out pair, where the labels actually sit next to each other. Their order matters more than
usual: **living room above, power below**, because the living-room band is the one
a passer-by may tap by accident and power is the one that must never be
mis-targeted into a *navigation* — though note that neither opens anything that
changes the house. The only tap in this application with consequences is the PWM
commit on detail screen 1, and that is two taps and a confirmation away (§8.6).

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

**No gesture writes to the bus.** Every gesture above is navigation. Nothing in
the gesture table can commit a duty, because a gesture is the one input on this
panel that a user cannot see they are giving (§7.9, F19).

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

Shared skeleton for all five detail screens:

- **Header**: title, large live value, unit, and a **range selector** for the
  history chart (1 day / 1 week / 1 month) as a 3-way segmented control.
- **Chart**: 440 × 260 px at x=20, y=200.
- **Readouts** below the chart: min / max / mean / now over the selected range, plus
  a "data age" line that says `12 min ago` if the newest sample is stale.
- **Back button**, 48 px arrow, top-left.

Chamber-specific content:

- Target temperature with a horizontal reference line on the chart (dashed).
- Time above target, accumulated over the selected range. This is the number that
  actually tells you whether the heater is efficient, and it cannot be derived from
  a snapshot.

#### 8.6.1 The duty control

This is the only part of the application that changes the house, so it is the only
part of the UI that is designed around being *slow on purpose*.

```
  y=470  +---------------------------------------------------+
        |  PWM DUTY                          [RESTORED]   |   source badge
        |                                                   |
        |  OFF  ├──────────●────────────────────┤  FULL    |
        |           0 %                   100 %             |   slider, 56 px
        |                                                   |
        |  in force: 55 %  (from 0x280)                     |   24 px, dim
        |  requested: —                                    |   24 px
        |                                                   |
        |  This value survives a power cut.                 |   20 px, warn
        +---------------------------------------------------+
        |                                                   |
        |              [   APPLY  55 %   ]                  |   440 x 64, y=580
        +---------------------------------------------------+
```

**The control itself.**

- A single `lv_slider`, 0 → 1000, with **0 at the left labelled OFF** and 1000 at
  the right labelled FULL. The direction is high time and is not negotiable —
  `pwm_out.cpp:124` drives the pin low at permille 0 (§7.9).
- The slider **snaps to 5 %** (50 permille). Below that the output stage clamps to
  10 permille anyway (`pwm_out.cpp:138`) and the extra resolution is theatre; above
  it, the useful span is coarse. Finer steps would make the control feel precise
  while the plant is not.
- **Dragging changes nothing.** The slider position is a *pending* value, shown in
  the `requested:` line in the accent colour. No frame is transmitted until APPLY
  (F19). This is the single most important line of the whole design.

**The confirmation.**

- APPLY is disabled while `requested == in force`, so a no-op cannot be sent.
- Tapping it opens a **modal confirm** naming the change in plain words:
  *"Set PWM duty to 55 %? This survives a power cut."* with **Confirm** and
  **Cancel**. The modal exists because the action is durable and because the
  consequence of a mis-tap is not undoable by tapping again.
- Cancel returns the slider to the in-force value.

**What is shown afterwards (F18, F20, F21).**

| State | Display |
| ----- | ------- |
| Sent, no report yet | `requested: 70 % · sending…`, APPLY disabled |
| Reported `0x280` matches | slider snaps to the reported value, badge goes **live**, `PWM_SRC_CAN` |
| No report within the timeout | **failed**, badge red, slider reverts to the last reported value |
| Report shows a *different* duty than sent | **conflict**, badge amber, both values shown side by side |
| `PWM_SRC_EEPROM` on boot, no command seen | badge **RESTORED**, hollow marker in the status strip (§8.2) |

The "report shows something else" case is the interesting one: it means another
controller — the AVR's own logic, or the nRF path — is writing the duty too. The
panel's job there is to show the truth and stop, not to keep pushing its own value
in a loop. There is no retry.

**No minimum, no maximum beyond the ends.** The panel applies no clamp of its own
(N15, R20); if the safe upper bound is one day moved into the AVR, the slider will
follow it automatically because it reads the reported value back.

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
(F5 lists five zones; the power area is the fourth).

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

- Row height 50 px instead of 18, Montserrat **21 px** (the 4 mm floor, §8.1.1 —
  rev 1.2's 20 px was 3.81 mm and below it), 480 px wide. The row count becomes
  `(800 - 44 header - 88 pane) / 50 ≈ 13` rows, down from rev 1.2's 16 and well
  below project 26's 23 on a 3.5" landscape — and each row is now genuinely
  legible at arm's length rather than merely legible up close.
- Tapping a row opens the existing detail overlay, moved to a full-screen
  `lv_obj_t` on `lv_layer_top()` rather than a separate LVGL screen, preserving the
  approach that already works in project 26.
- The monitor's own header carries the bus-error banner from §7.2 in place of the
  home screen's status dot.
- A **range selector is not shown** — the monitor is live-only by design. History
  lives in the detail screens; mixing the two would double the reading model
  for no benefit.

### 8.11 Navigation — the bottom pane

One screen manager, deliberately small:

```cpp
namespace ui {
enum class Screen { Home, Chamber, WaterIn, WaterOut, Power, Living, Settings,
                    Can, Log };
void push(Screen s);     // records the stack, loads
void pop();              // -> previous, or Home at depth 1
void jump(Screen s);     // nav tab: loads s at depth 1, clearing the stack
Screen current();
}
```

#### 8.11.1 The pane

```
 +---------------------------------------------------+  712
 |                                                   |
 |     ^       ^       ^       ^      ^      ^      |   7 slots, 68 px each
 |    HOME   CHAMBER  WATER  WATER  POWER LIVING  ^   |   icons, 32 px
 |               IN     OUT                  gear    |
 |  ───────────────────────────────────────────────  |   3 px accent rule
 +---------------------------------------------------+  800
```

Seven slots at 68 px, which is 13 mm — over three times the type floor, so the
pane is usable by touch alone at arm's length and does not depend on the 4 mm rule
being read correctly. It is present on **every** screen except the log screen,
which is full-bleed by design so that as many lines as the floor allows are
visible (§8.12).

Icon-only, because seven 4 mm labels do not fit across 480 px — each would get
38 px of width. The label lives in the screen header instead, where there is room
for it. The active tab is a filled glyph plus a 3 px accent underline; the
current screen's name is in the header at `FONT_VALUE`.

**The pane and the stack are two different things, and the difference is the
whole design.** `jump()` is a tab: it replaces the screen and resets depth to 1,
because a tab is a *place*, not a step. `push()` is a link: the water-out screen
is reachable from the water-in screen's comparison view, and that is a step
*forward*. So:

- Tapping any tab always lands you on that screen with nothing behind it.
- **Back still exists** — the 48 px `LV_SYMBOL_LEFT` in the header, unchanged,
  because F7 requires it and because there is genuinely somewhere to go back *to*
  after a link. It is not a duplicate of the tabs: it pops the stack, the tabs
  replace the screen.
- Three-finger tap remains Home, from anywhere.

**Why the zones and the tabs coexist rather than one replacing the other.** The
tabs make every screen reachable in one tap from anywhere, which is the
accessibility win. The zones keep the spatial model — the number you are looking
at is the thing you tap — which is why they survive on a screen that no longer
needs them for discoverability. Neither is load-bearing for the other.

- **Back button** is a 48 px `LV_SYMBOL_LEFT` button, top-left, 8 px inset,
  present on every screen except home. Implemented as an LVGL button, not a
  coordinate test — a scrollable container swallows a tap as a scroll on
  resistive touch, and this avoids repeating that class of bug.
- **Transitions** are an instant swap plus a 120 ms cross-fade on the content layer
  only. The background image does not fade, so the screen never flashes black.
- **Idle return**: after 3 minutes with no touch, return to home. A wall panel left
  on a detail screen is a wall panel showing a stale number to whoever walks past.
  A persistent pane makes this *less* important — the previous screen is one tap
  away and visibly still there — but it stays, because the home screen is the one
  worth returning to.
  The log screen is exempt while follow-at-end is on — an unattended log view
  scrolling itself is the expected behaviour, not a stale display.
- **The chamber screen's idle return is suppressed while a duty change is
  pending.** Returning to home with an unsaved slider position would discard the
  pending value silently, which is a worse outcome than staying put. Either the
  change is confirmed, or the screen stays until it is resolved.

### 8.12 Log screen

Reached by long-pressing the home screen's status strip (§8.5). This is the
project's diagnostic surface, and it has to work with no network, no serial and no
debugger — the three things that are gone exactly when it is needed.

```
 y=0
 +---------------------------------------------------+  0
 | [< Back]   LOG        E W I D   [INV] [CLR] [SAVE] |  toolbar, 44 px
 +---------------------------------------------------+  44
 | 1234.567 E esp_lcd: panel_io: timeout              |  monospace 21 px,
 |         on data                                    |  wrapped at 38 chars
 | 1234.589 W twai: bus error, REC=128                |  ~26 lines
 | 1234.601 I haus: 0x200 rank1 -> 62.14 degC         |
 | 1234.601 ! rank shift: rank 1 now 2876C25704E13CF9 |  amber
 | 1234.660 I haus: temp water-in 62.14 -> in[1]=1    |
 | 1235.002 W sd: flush took 412 ms                   |
 | ...                                                |
 +---------------------------------------------------+  760
 | 1 048 576 B | 73% | dropped 21418 | up 3d [FOLLOW] |  status, 40 px
 +---------------------------------------------------+  800
```

**Text view.** One `lv_obj` with `LV_OBJ_FLAG_SCROLLABLE` containing a single
label in a **custom monospace font** at **21 px** (§8.1.1 — rev 1.2's 16 px was
3.05 mm, well under the 4 mm floor), generated with `lv_font_conv`, ASCII
`0x20-0x7E` plus `°` and the level glyphs. `lv_label` rather than an `lv_list`,
because a list means one object per line and there are thousands. Wrapping mode is
`LV_LABEL_LONG_MODE_WRAP` (rev 1.2 named `LV_LABEL_LONG_CLIP`, the LVGL 8 spelling,
which does not exist in 9.x).

The floor costs density and the numbers are worth stating: at 21 px monospace the
line is **38 characters**, not 50, and the text area holds **~26 lines**, not 44.
Wrapping rather than clipping is the deliberate choice — a stack trace cut off at
column 38 is useless, whereas one that wraps is merely taller. The window logic
below is unchanged and operates on wrapped visual lines.

**Windowed rendering.** The label is never set to the whole 1 MB. Only the visible
window — about 26 lines at 21 px over the ~628 px of text area — is rendered
into the label, and the window start is recomputed when the scroll position or the
log head changes. This is
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

**No nav pane on this screen.** It is full-bleed, so the 88 px a pane would take
buys eight more log lines — a third more console, on the one screen where console
density is the entire point. Back therefore stays in the toolbar rather than moving
to the pane, which is the one place the navigation model bends.

### 8.13 Detail screen 5 — Living room

The fifth detail screen, for the local AHT20 + BMP280 (§7.8). It uses the shared
skeleton, and it is the first screen that has to present **three quantities with
three different units and three different visual treatments** at once.

```
  y=0
  +---------------------------------------------------+  0
  | [<]  LIVING ROOM                    I2C ok        |
  +---------------------------------------------------+  44
  |                                                   |
  |   21.8 °C                                          |   ~90 px
  |   47 %rh            1013 hPa                      |   ~36 px
  |                                                   |
  |   [ temperature ] [ humidity ] [ pressure ]       |   series tabs, 40 px
  +---------------------------------------------------+  200
  |                                                   |
  |                  chart, 440 x 260                 |
  |                                                   |
  +---------------------------------------------------+  460
  |  min 20.4   mean 21.6   max 23.1   now 21.8      |
  |  updated 34 s ago                                  |
  +---------------------------------------------------+  520
```

**Differences from the other four, and why each is necessary.**

- **A series selector, not just a range selector.** The range control (1 d / 1 w /
  1 m) is the shared one; the *series* tabs are local to this screen because it is
  the only one with more than one quantity. Three charts stacked vertically would
  fit in 800 px but each would be too short to read an envelope in.
- **Independent y-axes per series**, auto-scaled with the §9.4 hysteresis. Humidity
  moving 43 → 52 %rh and pressure moving 1008 → 1016 hPa are both meaningful and
  neither should share a scale with the other, or with temperature.
- **Non-temperature ramps** — single cyan for humidity, single dim violet for
  pressure, per §8.4. No blue→amber→red.
- **The BMP280 contributes pressure only.** Its temperature is read and discarded:
  in a real room the two chips disagree by a few tenths, and showing both would
  present the disagreement as a fault when it is just two sensors (§7.8).
- **`I2C` badge in the header**, per-device: green both present, amber one absent,
  grey both absent. Per N14 a missing sensor degrades this screen only.
- **No target line.** Unlike the chamber screen there is no setpoint to draw,
  because nothing in this project decides what the living room should be.

**A "sensor" sub-line**, one row under the readouts, names what is physically
attached: `AHT20 · BMP280 · H1`. On a panel mounted on a wall in a room, the
useful question is rarely "what is the humidity" and often "which probe is that
one again" — and the answer should not require a laptop.

### 8.14 Settings screen (under the gear)

Reached from the gear tab. Simple, mostly declarative, and the home of the one
setting that can strand the device.

```
 y=0
 +---------------------------------------------------+  0
 | [< Back]  SETTINGS                                 |   44 px
 +---------------------------------------------------+  44
 |  CAN BUS                                       [ ] |   switch, 56 px row
 |      Off gives back USB: no serial, no flashing.   |   21 px, warn colour
 |      CAN data, charts and the monitor are disabled.|
 |                                                   |
 |  BACKLIGHT                                     [ ] |   §6.2, binary only
 |                                                   |
 |  UNITS                                            |
 |      temperature  [C] [F]                          |
 |      clock        [24 h] [12 h]                    |
 |      power        [auto] [kW] [W]                  |
 |                                                   |
 |  RETENTION      24 months                          |
 |  HEATER TARGET  55 degC                             |
 |  SENSOR ROLES   3 bound, 1 unbound        [edit]   |   §7.7
 |  WATER MODEL    not installed             [ ]     |   §7.11
 |                                                   |
 |  UI FILE        /ui.json  v1             [edit]   |   §4.4
 |  DIAGNOSTICS    SAVE RING TO SD            [ ]     |   §7.6
 +---------------------------------------------------+  712
 |  HOME  CHAMBER  WATER  WATER  POWER LIVING   [gear]|   active: gear
 +---------------------------------------------------+  800
```

**The CAN switch is the only row that needs a confirmation, and it is not a
boolean that flips.** It is a two-state control with a stated consequence and a
modal, for the same reason the duty slider is (§8.6.1):

- Toggling **off** asks: *"Turn CAN off? USB returns; there will be no CAN data,
  no history and no monitor until you turn it back on."*
- Toggling **on** asks: *"Turn CAN on? USB disappears — no serial, no flashing.
  If the firmware then fails, recovery needs a power cycle."*
- The switch shows the **actual** state read back from the CH422G pattern, not the
  requested one — the same requested-versus-actual discipline as the duty slider
  (F21). A config that says CAN is on while the mux is low is a fault, and it is
  shown as one.
- The setting persists immediately and is logged.

**Why Settings is the right home and not the status strip.** The status strip is
for *state*; this is for *intent*, and a destructive-sounding intent control
belonging next to a fault indicator invites mis-taps. It also gives §7.7's
`sensor_roles[]` editor and §4.4's `/ui.json` editor a place to live, which
otherwise have nowhere.

**This screen is the strongest case for §4.4's declarative option 3.** Almost
every row is a label, a value and a control. Only the CAN row has behaviour behind
it. So the *row list, order, labels and value ranges* come from `/ui.json`, and the
handlers are looked up by key — so adding "Screensaver timeout" later is a JSON
edit rather than a rebuild, while the CAN confirm modal stays hardcoded because it
is not a row.

### 8.15 Storage Status

Where the card's state lives, and what each state actually *means*. The status
strip already carries an SD dot (§8.2); this is the surface behind it.

#### 8.15.1 Three states, defined by evidence

`tpl_sd` distinguishes only "not initialized", "mount failed" and "no card"
(`tpl_sd.cpp:57-66`). That is enough for *absent* and not enough for *faulty*,
which is the state that matters — a card that mounts and then fails to write is
the one that loses history quietly.

| State | Evidence | Home strip |
| ----- | -------- | ----------- |
| **Operational** | mounted; the last rollover write returned success; the newest `/rollup.bin` verified against its CRC | green dot |
| **Faulty** | any of: a rollover write failed; a CRC mismatch in `/rollup.bin` or a daily file; free space below the 50 MB floor; three consecutive failed reads | red dot |
| **Absent** | mount failed, or `SD.cardType() == CARD_NONE` | grey dot |

**Faulty is sticky and says when it happened.** It clears only after a successful
rollover, and the storage panel keeps the timestamp of the first failure, because
"the card went bad at 03:14" is worth far more than "the card is bad". This follows
the same rule as the CAN bus-fault flag (§7.2): a panel that silently stops
updating is worse than one that says so.

#### 8.15.2 The figures

| Field | Source | Note |
| ----- | ------ | ---- |
| Card type | `SD.cardType()` → MMC / SDSC / SDHC | as `tpl_sd` already formats it |
| Total | `SD.totalBytes()` | |
| Used | `SD.usedBytes()` | |
| **Free** | total − used, **and** the floor headroom | shown as both, because "37 GB free" and "37 GB free, floor is 50 MB" are different facts |
| Last rollover | recorder timestamp | "4 h ago" |
| Newest history file | recorder | the date, so a stale rollup is visible before a chart is opened |
| Per-tier usage | §10.5.4 | samples / rollup / log dumps |

#### 8.15.3 Where it surfaces

| Surface | What it shows | Why there |
| ------- | ------------- | --------- |
| Home status strip | the **dot only**, plus the free-space figure **only when it is low** | "37 GB free" is noise on a glanceable screen; "12 MB free — floor 50 MB" is not |
| **Settings → Storage** | the full panel: a used/total bar, card type, free and headroom, last rollover, newest file, per-tier breakdown | somewhere a person goes deliberately (§8.14) |
| Log screen status bar | one compact field: `SD 1.0G/29G ok` | the log screen is what is open when something has gone wrong |
| `/sd/status` JSON | the same numbers, machine-readable | already the shape of `tpl_sd_status`; extend rather than reinvent |

#### 8.15.4 A problem this exposes in §6.7

**`tpl_sd` cannot mount on this board at all**, which §6.7's Option C did not
account for. `tpl_sd` drives a real chip-select **GPIO** — `pinMode(TPL_SD_CS,
OUTPUT)` and `SD.begin(TPL_SD_CS, sdSPI, …)` (`tpl_sd.cpp:29-30`, `:39-41`) —
but on this board CS is a CH422G bit with no GPIO behind it (§3.4, §6.5).
So `/sd/list`, `/sd/read` and `/sd/download` are all dead here, and §6.7's claim
that "`tpl_sd` stays as the *browser*" does not hold. The storage figures above are
therefore read from **the recorder's own SD handle**, not from `tpl_sd` — which is
the right call anyway, since the recorder owns the card (§6.7).

What remains open is the **browser**: either move `/sd/list` and `/sd/download` into
the recorder service, or revisit the `tpl_sd` CS hook that §6.7 rejected as
"an untyped function pointer for a two-call sequence". The second option now looks
less bad than it did, because `tpl_ws7_sd_select()` is a properly typed named
accessor rather than a bare function pointer. **Q20.**

## 9. History Charts

### 9.1 Requirement

F8: every detail screen carries a history chart switchable over **1 day, 1 week,
1 month**. The charts are the reason the SD card is on the board, so they are a
first-class feature of this project, not an extra.

### 9.2 Time ranges and point budgets

Base resolution is one sample per minute (§7.4), record size 24 B (§7.4). At the
chart's actual width of 440 px (§8.6):

| Range | Samples | Raw span | Samples / column | What the column shows |
| ----- | ------- | -------- | ---------------- | -------------------- |
| **1 day** | 1 440 | 34 560 B | 3.3 | min–max envelope + mean dot |
| **1 week** | 10 080 | 241 920 B | 22.9 | min–max envelope + mean line |
| **1 month** | 43 200 | 1 036 800 B | 98.2 | min–max envelope + mean line |

"Raw span" is the size of the underlying samples. After §7.10 the week and month
views do not read it; they rescale the rollup (§9.5). 1 440 × 24 = 34 560 exactly.
Earlier drafts wrote the day buffer as 33 792 (33 × 1024), which is not a whole
number of records.

At roughly three minutes per column even the one-day view is over-sampled, which
is why the chart is an **envelope**, not a polyline: a single 90 s thermostat cycle
spans a third of a column even in the one-day view, and is invisible in a plain
average at 1 month.

A raw month (1 036 800 B, thirty days of 34 560 B) fits in the 8 MB PSRAM, so it
*could* be cached wholesale. It is deliberately not (§9.5). Retention (§9.7):
24 months is 24 × 1 036 800 B ≈ 25 MB of daily files, plus one 65 472 B rollup,
which still sits under the 50 MB free-space floor.

### 9.3 Record format and downsampling

Two-tier. The **store** is fixed-size binary; the **view** is a downsampled
envelope.

```
store:  /data_YYYYMMDD.bin   1 440 × 24 B = 34 560 B, written whole at midnight (§7.10)
        seek within a day file: index * 24                O(1)
        week and month views do not read these files (§9.5)

view:   struct ChartColumn { uint32_t t; int16_t mn[2]; int16_t mx[2];
                             int32_t  sum[2]; uint16_t n[2]; uint8_t flags; };
        440 columns x 28 B = ~12.3 KB
```

Only **two** series are held per column set, not four. The water-out screen is the
only place a second line is drawn (§8.8), and the living-room screen switches
between series rather than overlaying them (§8.13), so two is the true maximum and
carrying four would be a third of the structure spent on nothing.

Downsampling is done **streaming**, never by loading the window first:

```
for (chunk of 512 records:            # 12 KB working buffer
    read  -> Sample[512]
    for each sample:
        col = (sample.epoch - t0) * px / (t1 - t0)
        accumulate mn/mx/sum into ChartColumn[col]
finalise: mean = sum / n, flagged where n == 0
```

Peak RAM for a chart build is therefore **~24 KB** (12 KB chunk + 12.3 KB
columns), not 1 MB. This is what keeps N6 honest, and it means a 32 GB SD card is
enough for well over two years rather than eight weeks.

**Each quantity is validated against its own sentinel before it reaches a
column.** The three CAN temperatures can be `HAUS_CAN_TEMP_EMPTY`; the three
climate fields use the `CLIMATE_VALID` bit (§7.4). A column that mixes valid and
empty minutes aggregates only the valid ones and its `n` records how many there
were — so a night where the probe dropped out for 40 minutes shows a *shorter*
envelope over the same span rather than a false spike toward the sentinel.

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

### 9.5 Reading windows

With the three-tier store (§7.10) this section got much shorter, and that is the
point of the redesign. **Only the 1-day view reads raw samples; the week and month
views read pre-aggregated columns out of PSRAM and never touch the card.**

| Range | Source | Cost | Reads SD? |
| ----- | ------ | ---- | --------- |
| **1 day** | L1 day buffer, 1 440 × 24 B in PSRAM | ~1 ms, no I/O | no |
| **1 week** | L2 rollup, 168 hourly columns | <1 ms, a rescale | no |
| **1 month** | L2 rollup, 744 hourly columns | ~1 ms, a rescale | no |
| *boot only* | `/rollup.bin` + newest daily file | ~100 KB, ~55 ms | once, before first frame |

Rev 1.2's 500 ms month build, the recorder-task offload, the four-entry LRU chart
cache and the "chart loading" spinner **all disappear**. They existed to hide a card
read behind an asynchronous hand-off, and there is no longer a card read to hide.

What is left, and it still matters:

- **The recorder must have rolled over cleanly** before a range query. If the daily
  write failed, the rollup is at most a day stale and the chart header says so.
- If the card is absent, or the rollup file is missing or the wrong version, L1 and
  L2 are empty and the chart area shows **"no data"** — never an empty axis that
  reads as "nothing happened".
- **A stale rollup is labelled.** If the newest column is older than the current
  hour, the chart carries a "history as of HH:MM" note. A month chart that silently
  stops a day early is the kind of thing that is only noticed months later.

The downsample algorithm in §9.3 is still needed — it is what builds each hourly
column from the minute samples — but it now runs once per hour on the recorder
task instead of once per query on the UI's behalf.

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

The chart is 440 px wide (§8.6), so one column is 1 440 / 440 ≈ 3.3 minutes at
1 day and 43 200 / 440 ≈ 98 minutes at 1 month. The bubble therefore shows the *column's* aggregate, not a
fabricated instant value. Claiming a precise reading the data cannot support is how
history views lose trust.

### 9.7 File rotation and retention

- **One file per day**: `/data_YYYYMMDD.bin`, 34 560 B, written **once** at local
  midnight by the recorder task (§7.10). Two writes a day in total, including the
  rollup — against the 1 440 a per-minute append would cost.
- The file is named by the wall clock, and the day boundary comes from the **last
  sample's epoch**, not from `localtime()` alone, so an NTP step cannot produce a
  file with a nonsense name or split a day across two files.
- **Loss window: up to 24 hours.** Between rollovers the day's minute data exists
  only in PSRAM. A power cut loses it, and §9.3 renders the loss as gaps rather
  than interpolating across them. This is the deliberate side of the write-endurance
  trade (§7.10, Q16, R23).
- **Retention: 24 months**, configurable in SPIFFS config, at 1 036 800 B/month ≈ 25 MB.
  On startup and daily, the oldest files beyond the limit are deleted. Deleting is
  never more than one file per pass, so a card with 30 stale months does not stall
  the recorder.
- **Free-space guard:** if free space drops below 50 MB, retention is forced to
  3 months and the home screen's SD dot turns amber. Losing a month of history is
  acceptable; filling the card and losing *everything* is not. The floor was set
  for the old 12-byte record and is deliberately *not* lowered for the 24-byte one
  — 50 MB is still ~3 months of headroom on a 64 GB card, and the guard exists to
  catch a genuinely full card, not to ration.
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
| `sensorTask` | 0 | 2 | 4 KB | 30 s | AHT20 + BMP280 reads (§7.8), holds the I²C bus mutex |
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

**I²C is the second contended resource**, and its rule is stronger than the log
ring's because it cannot drop work: a transaction that is interleaved with a touch
poll does not degrade, it returns garbage. One bus-wide mutex covers the CH422G,
the GT911, the AHT20 and the BMP280 (N13), taken with a bounded timeout so a stuck
sensor task cannot wedge the touch read callback. The touch poll is the one caller
that may never block for long — if the bus is busy, the poll reports "no finger"
this cycle rather than waiting. Priority order on contention is touch > expander >
sensor, because a missed touch is visible to a person and a late climate sample is
not.

### 10.2 State hand-off

```cpp
enum class SensorFn : uint8_t { Chamber, WaterIn, WaterOut, Board, None, COUNT };

struct HausState {          // POD, trivially copyable
  // --- CAN, by slot: what the CAN monitor screen shows ---
  int16_t  temp_c100[HAUS_CAN_MAX_SENSORS];  // HAUS_CAN_TEMP_EMPTY == absent
  uint8_t  fn_of_slot[HAUS_CAN_MAX_SENSORS]; // SensorFn, or None

  // --- CAN, by function: what the home screen and recorder consume ---
  int16_t  temp_by_fn[(uint8_t)SensorFn::COUNT];

  // --- local I2C (not CAN, never slot-indexed — §7.8) ---
  int16_t  living_c100;
  uint16_t hum_c100;
  uint32_t pressure_pa;
  uint8_t  climate_flags;   // CLIMATE_VALID_AHT20 | CLIMATE_VALID_BMP280

  // --- actuator (§7.9) ---
  uint16_t pwm_permille;    // reported by 0x280, never by what we sent
  uint8_t  pwm_src;         // PWM_SRC_EEPROM / _CAN / _NRF / _FAULT
  uint16_t pwm_pending;     // slider position; 0xFFFF == none pending
  uint32_t pwm_pending_since_ms;

  // --- power ---
  uint16_t power_dw;        // decawatts, same unit as Sample.power_dw (§7.4)
  uint8_t  power_src;       // NONE / SML / ADC

  uint32_t rx_frames, tx_frames, bus_errors;
  uint32_t last_frame_ms;
  uint8_t  can_fault;       // sticky until a frame is received again
  uint8_t  sd_fault;
  uint8_t  i2c_fault;
};

extern HausState g_state;         // written by canTask and sensorTask
extern volatile uint32_t g_state_seq;   // bumped on every write
```

**Two writers now, so the fields are partitioned by owner** rather than shared.
`canTask` owns everything from `temp_c100` down to `pwm_pending` and the counters;
`sensorTask` owns only the four climate fields. Neither touches the other's block,
so `g_state` needs no lock — but the sequence counter is bumped by both, and a
reader that copies the struct may get one owner's values a few milliseconds apart
from the other's. That is the same accepted trade as before and for the same
reason: every field is independently meaningful, and the alternative — a mutex on
the hot path of a 400 Hz display loop — is worse.

**`pwm_permille` and `pwm_pending` are deliberately different fields.** The first
is what the AVR says; the second is where the slider is. Collapsing them into one
"the duty" variable is the specific bug that would let a failed send keep showing
as a success, which is why F18, F21 and §8.6.1 all phrase everything in terms of
*requested* versus *in force*.

### 10.3 Refresh budget at 480 × 800, RGB565

| Work | Frequency | Cost |
| ---- | --------- | ---- |
| Header + clock | 1 Hz | small; measure in phase 7 |
| Flame animation | 20 Hz, ~120 × 140 px invalidated | see below |
| Pipe flow dashes | 20 Hz, ~2 × 60 × 24 px | see below |
| Chart rebuild | at most 1 / min, from RAM (§9.5) | ~1 ms, not a fraction of the flush |
| Background image | never redrawn | 0 |

The one hard number is the panel's scan-out. 800 × 480 = 384 000 px, and the
pixel clock is 16 MHz, so the active pixels take 384 000 / 16e6 = **24 ms**
(41.7 Hz). The porches in §6.3 add (4+8+8) by (4+8+8), so a full frame is
820 × 500 = 410 000 clocks ≈ **26 ms**. An earlier draft divided the same pixels
by 16 MHz and got 38 ms, then treated a 20 Hz full-screen flush (20 × 38 ms ≈ 76 %
of a core) as the cost of the animated regions. Both the number and the
assumption were wrong: LVGL invalidates the flame and the dashes, not the frame.

Those regions are about 120 × 140 + 2 × 60 × 24 = 19 680 px. At 20 Hz that is
0.39 Mpx/s, about **2.5 %** of the 16 Mpx/s the pixel clock can move. That is the
argument R3 needs. It is an estimate of pixels, not a measured core load — DMA,
bounce buffers and LVGL's draw walk are still on top of it. Phase 7 measures the
home screen against this section. A full-screen invalidate (the screen object,
or a theme change) is still a 24 ms scan-out and will stutter.

LVGL's `LV_DISPLAY_RENDER_MODE_DIRECT` with partial buffer support handles this; the
draw buffers are sized `TPL_WS7_DRAW_LINES` lines and live in internal SRAM.

### 10.4 Timing budget

- **Home screen redraw:** ≤ 5 ms typical.
- **Touch → visual feedback:** ≤ 2 frames (< 100 ms). Anything slower reads as a
  broken panel.
- **Screen transition:** 120 ms cross-fade on the content layer.
- **Chart build:** ~1 ms, because the week and month views read pre-aggregated
  columns out of RAM (§7.10) rather than streaming the SD card. The only build
  that touches the card is the 1-day view on a cold boot, ~20 ms.
- **Boot history restore:** ~100 KB read from SD (§7.10) — under 100 ms, done
  before the first frame so no chart ever renders half-populated.
- **Idle return to home:** 3 minutes.

### 10.5 Memory Map

Four pools, each with a different rule. The rules matter more than the totals:
the failure mode this project fears is a panel that boots and then degrades, and
every allocation below is chosen so that the *fixed* costs are known at compile
time and the *variable* ones are bounded and small.

#### 10.5.1 PSRAM — 8 MB, static, sized at compile time

Everything here is `static` or `heap_caps_…(MALLOC_CAP_SPIRAM)` **once** at init
and never reallocated. Nothing in this table is touched by the LVGL draw path
(N6).

| Purpose | Bytes | Notes |
| ------- | ----- | ----- |
| Debug ring | 1 048 576 | §7.6. `MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT`, never freed |
| **L1 day buffer** (minute samples, today) | 34 560 | §7.10. 1 440 × 24 B |
| **L2 hour rollup** (31 days) | 65 472 | §7.10. 744 columns × 88 B |
| Decoded background image | 768 000 | §8.3. RGB565, 480 × 800 |
| LVGL chart canvas | 228 800 | §9.4. 440 × 260 × 2 B |
| Downsampling chunk buffer | 12 288 | §9.3. 512 × 24 B |
| LVGL chart columns (build scratch) | 12 320 | §9.3. 440 × 28 B |
| LVGL objects, styles, font bitmaps | ~65 536 | estimate, asserted after `ui_init()` |
| WiFi + OTA + WebSocket buffers | ~65 536 | ESP-IDF / Arduino internals |
| **Subtotal** | **≈ 2.30 MB** | |
| **Headroom** | **≈ 5.7 MB** | the deliberate margin |

The headroom is the point. A 1 MB ring and a 768 KB image already commit 1.8 MB
before any history exists; the tiered store (§7.10) adds ~100 KB and the whole
history subsystem therefore costs **1.1 % of PSRAM**. That is what makes it
affordable to add a second and third retention tier later without re-planning the
board.

#### 10.5.2 Internal SRAM — 512 KB

The panel's DMA cannot cross the octal-PSRAM boundary, so the draw buffers must
live here (N6). This is the *only* pool where placement is not a free choice.

| Purpose | Bytes | Notes |
| ------- | ----- | ----- |
| **LVGL draw buffers** | 64 000 | **2 × 800 × 20 px × 2 B.** The hard internal-RAM cost (§6.3). Not 65 536 |
| TWAI driver + RX queue | ~20 480 | 4 096 B task + queue + status |
| Task stacks (6 tasks) | ~38 912 | loop 8 K, can 4 K, recorder 6 K, sensor 4 K, webserver 4 K, wifi 2.7 K + margin |
| SML reassembly pool | 774 | 3 × 258 B, lifted from project 15 (`sml_buffers[3]`) |
| FreeRTOS idle/ISR/heap remainder | ≈ 380 000 | |
| **Committed** | **≈ 124 200** | **≈ 24 % of internal SRAM**. Draw buffers are 64 000 B, not 65 536 |

#### 10.5.3 Heap — budget 64 KB (N6)

The only allocations left after the two tables above. Every one of them is
short-lived and none is on a per-frame path.

| Consumer | Peak | Notes |
| -------- | ---- | ----- |
| `/ui.json` + `/config.ini` parse (ArduinoJson) | ~9 000 | §4.4, transient, freed after boot |
| Webserver response buffers | ~16 000 | incl. `/spiffs/read` returning a file body |
| LVGL style objects at screen build | ~12 000 | once per screen, screens are never destroyed |
| ArduinoJson in `tpl_websocket` | ~4 000 | existing template use |
| Misc / fragmentation margin | ~23 000 | |
| **Total** | **64 000** | |

`heap_caps_get_free_size()` is logged after `ui_init()` and again after the first
SD write, and phase 12's soak test fails if either drops by more than 8 KB over
72 hours. A leak on a wall panel is invisible until it is not.

#### 10.5.4 SD card

| Tier | Path | Write pattern | Size |
| ---- | ---- | ------------- | ---- |
| Daily samples | `/data_YYYYMMDD.bin` | **once per day**, whole file | 34 560 B |
| Rollup (L2) | `/rollup.bin` | once per day, whole file, rewritten | 65 472 B |
| Log dumps | `/log_*.txt` | on demand + before every reset (§7.6) | up to 1 MB |
| Assets | — | never (they are in SPIFFS, §4.4/Q4) | — |

**1 036 800 B per 30-day month** of daily files (30 × 34 560), plus one 65 472 B
rollup. At 24 months that is ≈ 25 MB, and the 50 MB free-space floor (§9.7) is
untouched. The old 1.01 MB figure used 33 792 B/day.

The write count is the design point, not the size. A per-minute append is 1 440
writes/day; writing the day once is **one**. That is the entire reason for the
tiered store (§7.10) — SD cards fail from write endurance long before they fill,
and a card in a wall panel that nobody will ever think to replace should not be
written 525 000 times a year to store data that does not change within the day.

## 11. Risks and Mitigations

| # | Risk | Impact | Likelihood | Mitigation |
| - | ---- | ------ | ---------- | ---------- |
| R1 | Enabling CAN *may* kill the CH9102 console, so a firmware bug becomes a bench visit | High | Unknown until Q22 | OTA is the production path (§7.5). N19 drives `USB_SEL` low before config is read. The console that was measured is the CH9102 on GPIO 43/44, and ROM download already worked (§3.1.1). Whether `USB_SEL` high removes that path is Q22. The mechanical switch can remove it on its own (D50) |
| R2 | `Serial` is `HWCDC`, so all logging vanishes exactly when CAN is on | High | Medium | Mitigated three ways: the PSRAM ring is authoritative and needs no serial; the WebSocket sink gives a desk console; the log screen gives an on-panel console. Remaining question is only whether `uart_set_vprintf` can also capture the port's own output (§12 Q2) |
| R2b | Writing to an unenumerated USB CDC blocks the logging task, turning "no console" into "hung system" | High | Medium | `tpl_log_set_serial(false)` at CAN enable (§7.6); ring writes use a 5 ms mutex timeout and drop rather than block |
| R2c | WebSocket writes from the logging path stall on a paused client | Medium | Medium | The ring is the buffer; the drain happens on the existing WS task at 750 ms with an 8 KB/tick cap (§7.6) |
| R2d | A log storm makes the panel unresponsive | Medium | Medium | 5 Hz on-screen rebuild, WS backpressure markers, level filters, dropped-line counter visible (§8.12, N12) |
| R3 | RGB flush bandwidth makes the UI stutter at 800 × 480 | Medium | Medium | Partial redraw only. A full frame is 24 ms of active pixels (§10.3); the animated regions are about 2.5 % of the pixel clock, which is an estimate. Measure on the home screen in phase 7 |
| R4 | LVGL 9.2 rotation silently no-ops without `LV_DRAW_TRANSFORM_USE_MATRIX` | High | Medium | Assert the macro at compile time in `tpl_ws7_display()`; a wrong screen orientation is otherwise very hard to diagnose |
| R5 | `HAUS_CAN_TEMP_EMPTY` (`INT16_MIN`) plotted as a real temperature | High | Medium | Sentinel checked at the decoder boundary; empty slots render grey and are excluded from chart statistics |
| R6 | History gaps interpolated, asserting data that was never measured | Medium | Medium | Gaps marked and drawn as gaps (§9.3); never bridge them |
| R7 | `tpl_sd` and the recorder fight over the card (open/read across a write) | High | Medium | Single ownership: recorder writes, `tpl_sd` reads only after a flush barrier (§6.7) |
| R8 | SD card fills and silently stops recording | Medium | Medium | Free-space guard, retention floor, amber status dot (§9.7) |
| R9 | No PWM backlight — the panel cannot dim, unlike the CYD boards | Medium | High | **Content dimming**, analysed in §7.13: dim the UI toward the background colour and leave the backlight lit. Free, no flicker, no bus traffic. The alternative — chopping CH422G IO at 100 Hz — is rejected there on three grounds (no hardware PWM behind I²C, convex charge-transfer response, flicker at the useful frequencies). A clean hardware fix exists (move EN to a GPIO, use LEDC) but needs GPIO 4, which §7.12 also wants: **Q19** |
| R10 | Refactoring `26_CYD_CAN_monitor` breaks project 26 | High | Medium | Extract behind a stable API; project 26 must keep building and passing (§7.3 step 5). It is the regression test |
| R11 | Host harness diverges from the device because of `#ifdef` drift | Medium | Medium | All conditional code lives below `services/`; the harness builds the *same* app/ and ui/ sources, never a copy. A CI job builds both targets on every change |
| R12 | Decoded background image (768 KB) plus draw buffers exhausts PSRAM/internal RAM | Medium | Medium | Static allocation, no heap; budget in §10.5; assert free heap after init in phase 1 |
| R13 | NTP not yet synced at boot → clock and chart axis are wrong | Low | Medium | Gate the clock on the NTP flag; show `--:--` rather than 1970. Charts start from the first valid sample |
| R14 | Touch calibration wrong in portrait, so zones are offset | Medium | Low | GT911 is already panel-native (§6.4), so no calibration table exists to be wrong; verified by the four corner taps in phase 3 |
| R15 | A sensor is unplugged, ranks shift, and the home screen shows a right number under a wrong label | High | Medium | F14 / §7.7: resolve `slot → rank → ROM → function`; rank→ROM is learned from identity frames and **expires** after 90 s; a shift is reported, never silently relabelled |
| R16 | No water-out sensor exists, so the third zone has no measured source | Medium | **Certain** | §7.11 shows a modelled value only when a `WaterModel` is installed, and labels it. Until phase 21 supplies one, the zone shows `—`, never rank 2's board temperature. A fitted DS18B20 supersedes the model |
| R17 | `tpl_debug_log` changes break the projects that already use it | Medium | Low | Opt-in via `USE_TPL_DEBUG_LOG`; keep `DEBUG_LOG_LINES`/`DEBUG_LOG_LINE_SIZE` working as aliases for the byte-size defaults so existing builds are unaffected |
| R18 | **I²C contention corrupts a reading.** Four devices share GPIO 8/9 and the GT911 is polled from the LVGL read callback, so a touch poll can land in the middle of a BMP280 register burst | Medium | **High** | One bus-wide mutex, not §6.5's narrower expander lock (N13); priority order touch > expander > sensor; the touch poll reports "no finger" rather than waiting; per-device CRC (AHT20) and chip-id + calibration checks (BMP280) turn a corrupted read into a flagged invalid rather than a plausible wrong number (§7.8) |
| R19 | **The climate sensors are absent, dead, or on the wrong header** and the fifth zone shows a plausible-looking wrong value | Low | Medium | J8-vs-H1 documented explicitly (§3.5) so the build is not wired wrong; per-device validity flags and a header badge; absent sensors degrade one screen only (N14); never a fallback to another source's value |
| R20 | **A commanded duty can still be full-on.** `pwm_out_set()` rejects only `permille > 1000` (`pwm_out.cpp:106`). 0 and 1000 are driven as solid levels (`:121-131`). Values from 1 to 9 and from 991 to 999 are pulled into 10…990 (`:138-139`). There is no ramp and no minimum pulse | **High** | **High** | **Not mitigable in this project** (N15). Any further clamp belongs in `AVR/01_heizung_temp`, where it protects every controller. The panel offers 0…1000 as asked, logs every commit, and shows only what `0x280` reports (F18/F21). Q12 |
| R21 | **A duty command is wrong and durable.** The value survives a power cut via the EEPROM restore point (`0x2C8`), there is no dead-man timeout by design, and nothing ramps it back | **High** | Medium | Confirmation modal naming the value and the durability (F19); nothing but the button transmits (F18, no gesture in §8.5); rate limiting and full logging (N16); the UI shows *requested* vs *in force* so a failed send is visible rather than assumed; the user can always set it back to OFF, which is one tap and the safest available action |
| R23 | **A power cut loses up to 24 h of minute history.** The day lives in PSRAM until the midnight rollover writes it | Medium | Medium | Accepted deliberately (§7.10): a per-minute write protects 1 minute at the cost of 525 000 card writes a year. The loss renders as gaps (§9.3), the rollup carries what survived, and `0x2C8`-style honesty means nothing is invented. Q16 records it as a judgement, not a derivation |
| R24 | **A modelled water-out temperature is read as a measurement** — by a person, or by a later contributor who wires it into a statistic | **High** | Medium | Structural, not editorial: a dedicated `TEMP_MODELLED` flag bit in the record, a dashed chart line, a permanent `MODELLED` badge, `~` prefixes on every derived value, and exclusion from all efficiency statistics (§7.11). A modelled value cannot silently become a measured one even if someone forgets which is which |
| R25 | **The 4 mm floor makes the panel less informative** — 26 log lines instead of 44, 38 characters instead of 50, 13 CAN rows instead of 16 | Low | **Certain** | Accepted (§8.1.1). The requirement is readability without glasses, and density was never the goal. Mitigated where it is cheap: soft wrapping rather than clipping, and the visible dropped-line counter so the loss of text is diagnosable rather than mysterious |
| R26 | **The SML decoder drifts.** It is lifted from project 15; if the two copies diverge, the panel and the e-ink display disagree about the household's power | Medium | Medium | Lift into `src/app/` as pure C++ (N5) and have **project 15 adopt the shared version**, so there is one implementation and project 15's build is the regression test — the same pattern as the `tpl_can_monitor` extraction (§7.3) |
| R28 | **A card mounts, then fails**, and history stops with the strip still green | Medium | Low | A sticky *faulty* state driven by evidence — failed rollover write, CRC mismatch, free space under the floor — not by absence of activity (§8.15.1). The recorder already retries every 30 s (§9.7); a fault is what that retry loop reports |
| R29 | **`tpl_sd` cannot mount on this board**, so `/sd/list` and `/sd/download` are dead and §6.7's "tpl_sd stays as the browser" is wrong | Low | **Certain** | Already true as built: `tpl_sd` drives a real CS GPIO and this board's CS is a CH422G bit. Storage figures are read from the recorder's own handle instead. The *browser* is the open half — **Q20** |
| R27 | **Enabling CAN in config strands the device**, if Q22 finds that `USB_SEL` high disconnects the CH9102 | **High** | Unknown until Q22 | N19 drives `USB_SEL` low first. A crash after step 3 of §7.12 leaves it high. The strap on GPIO 4 (Q17) is the fix no firmware bug can override, and it shares that pin with Q19. BOOT + the CH9102 already worked with the mechanical switch set correctly (§3.1.1, §7.12 option C). The expander's `0xFF` reset value is not this risk |
| R30 | **An AHT20 on H1 shares address `0x38` with CH422G WR-IO** | **High** | **Certain** if fitted as rev 1.4 specified | Do not fit it on this bus (§7.8). BMP280 at `0x76`/`0x77` does not collide. Q24 chooses the replacement. Until then the living-room zone stays `—` |
| R22 | Every other risk in this table gets worse because the panel can now write to the bus | Medium | **High** | R1 in particular changes character: a firmware bug that previously cost a display is now a firmware bug that can change the heater. That is why N15 forbids the panel from being an interlock, why the write path is a single button on one screen, and why the log ring records every transmit with its old and new value — the diagnosis for R1 is now a safety question, not a convenience |

## 12. Open Questions

Each of these changes the design, so none is silently decided above.

### 12.1 Resolved

| ID | Question | Resolution |
| -- | -------- | ---------- |
| ~~Q3~~ | Which sensor rank is chamber / water-in / water-out? | **Answered**, and the framing was wrong on the first pass. Rank is *not* bound to a function. The chain is `slot → rank → sensor ID (DS18B20 ROM) → function`: rank is a position in the sorted list of *currently present* sensors, so it shifts on unplug, while the function belongs to the sensor and travels with the probe. The `AVR/01_heizung_temp` README table (rank 0 = heater element → chamber, 1 = supply side → water in, 2 = rf-nano board → board) documents the **current installation only**. Consequences recorded in §7.7: rank→ROM is learned from the `0x180+r` identity frames and expires; ROM→function is local SPIFFS config and editable, because hardcoded ROMs would make the panel work on exactly one machine. Side effect: the current installation has no return-line sensor, so there is no water-out function to bind — that became **Q9**. |

| ~~Q1~~ | Is "current power" the household total from SML, or the heater's own draw from `0x2C0`? | **Answered: SML**, from `AVR/07_stromzaehler`. `ESP32/15_power_log_eink` already decodes the stream semantically with `libsml` and matches `1-0:16.7.0/255` (Stromeffektivwert, W) — so no protocol change and no new parser is needed, only a lift of `sml_publish()` into shared code (§7.4). The `0x2C0` chain in `AVR/01_heizung_temp/README.md` was derived from a resistor network since replaced, so it stays a unitless activity indicator (Q14). Transport detail: 33-frame bursts at one frame per 10 ms, plus a DLC-0 alive frame each second alternating `0x600`/`0x700` |
| ~~Q7~~ | Should the panel also *write* to the bus? | **Answered — yes, for exactly one frame.** The panel is the controller of record for the commanded PWM duty and writes `CAN_ID_PWM_SET` (`0x240`) on demand from a confirmation button (§7.9, F18–F21). Heater and pump are one control point on the existing bidirectional `0x240`/`0x280` pair, so no protocol change is needed. Everything else about being a listener stands: no ACKs, no configuration writes, no other nodes addressed. The `0x555` time broadcast in §7.2 is the existing house contract, not a second invention. The safety consequences are carried as R20–R22 and N15–N16 |
| ~~Q2~~ | Does the Arduino core open `Serial` on `HWCDC` or `UART0`? | **Answered in hardware, pinned in the build.** §3.1.1: the connector is a CH9102 on GPIO 43/44, and no `VID 0x303A` device enumerates. §7.1 sets `ARDUINO_USB_CDC_ON_BOOT=0` and `ARDUINO_USB_MODE=0`. Phase 1 confirms the running firmware's `Serial` is UART0. Nothing in this project calls `HWCDC` or `USB.begin()` |
| ~~Q4~~ | SPIFFS or SD for the background PNG? | **SPIFFS.** It has to be readable while the SD card is being written, and a card swap must not take the background with it (§8.3, D4). History stays on the card |

### 12.2 Open

| ID | Question | Impact | How it gets answered |
| -- | -------- | ------ | -------------------- |
| **Q9** | **There is no water-out temperature on the bus.** Where does the third zone's data come from? | Decides whether the third temperature is measured | **Partly answered:** option **(b)** is adopted for the display — derived from the commanded PWM duty, permanently labelled as modelled, excluded from every statistic (§7.11, F24, R24). The model function is **not yet known**; it is to be measured and supplied, behind a `WaterModel` interface with a null default. Option (a) — a 4th DS18B20 on the return line — remains the better answer and **supersedes the model automatically** when fitted (§7.11). Until either exists, the zone renders `—` |
| **Q5** | Does the current `tpl_sd` need to keep working on this board at all, given the recorder supersedes it? | Could delete ~200 lines from `tpl_sd` for this project, or keep the file browser for convenience | Recommend keeping `/sd/list` + `/sd/download` — browsing the recorded history over HTTP is genuinely useful. See also Q20: `tpl_sd` cannot mount here as built |
| **Q6** | Retention: 24 months at 1 036 800 B/month is ≈ 25 MB. Is more wanted? | Storage and the free-space guard threshold | Jochen; default 24 |
| **Q8** | Is EEZ Studio in scope for the home screen? | Either hand-authored LVGL (faster, more control, matches project 26's current state) or generated (draggable, but cannot express the flame/pipes) | Recommend hand-authored; EEZ cannot produce the vector heater or the envelope chart |
| **Q10** | Should the WebSocket log sink be authenticated? | `tpl_websocket` is unauthenticated on the LAN today; log content is house telemetry | Recommend accepting it on a home LAN, consistent with the existing unauthenticated `/update` OTA endpoint. If the answer is no, the change belongs in `tpl_websocket` for all projects, not here |
| **Q11** | Should `tpl_debug_log`'s PSRAM ring be made the template default for **all** projects, or stay opt-in? | Affects every existing project's RAM footprint | Recommend staying opt-in for now, defaulting on for boards with no console once the module is proven |
| **Q12** | **Where does a tighter PWM clamp belong?** `pwm_out_set()` already saturates at 1000, drives 0 and 1000 as solid levels, and pulls 1…9 and 991…999 into 10…990 (`pwm_out.cpp:106`, `:121-139`). Full-on is still legal. The panel offers 0…1000 | Decides whether 1000 permille is an acceptable command, and from which node a lower ceiling would be enforced | **Jochen + the AVR firmware, not this project.** A lower ceiling in `pwm_out_set()` protects every controller; one in the panel protects only the panel. The slider follows `0x280`, so the answer can arrive after the panel is built |
| **Q13** | Is the H2 I²C voltage jumper (`I2C 3V3 5V`, silkscreen 默认贴 3V3) actually 3V3 on the assembled board? | Both sensors are 3.3 V-only parts; on 5 V they are destroyed | Bench, phase 1: measure the bus with H1 unplugged. Cheap to check, expensive to get wrong |
| **Q14** | Is a `0x2C0` amp figure wanted on the wall at all? | The current chain in `AVR/01_heizung_temp/README.md` was derived from a resistor network since changed to 1k/1k/1k + 18k, so the counts no longer map to amps | Jochen. Recommend **no** until the chain is re-derived: show `0x2C0` as a unitless activity indicator, which is honest and still useful, rather than a kW or A figure that is merely plausible |
| **Q16** | Is a **once-a-day** SD write acceptable, given it loses up to 24 h of minute history on a power cut? | Card write endurance versus recovery granularity. 1 440 writes/day versus 2 | Jochen. Recommendation on record: accept. An SD card in an unattended wall panel fails from write count long before capacity, and the loss renders as honest gaps (§9.3) rather than as fabricated data. If the answer is no, revert to per-minute appends and reinstate §9.5's asynchronous chart build |
| **Q17** | Is a **hardware strap on GPIO 4** to force `USB_SEL` low acceptable — i.e. may the board be modified? | Matters only if Q22 says `USB_SEL` gates the CH9102. GPIO 4 is not claimed by `tpl_ledPin` (§6.7). It is also the pin Q19 wants | Jochen. Useful only after Q22. The CH9102 path already accepted esptool with BOOT held (§3.1.1) |
| **Q18** | Should the **CAN-enable liveness fallback** exist — drop back to USB if CAN is enabled but silent for N seconds? | Turns a silent bus into automatic USB recovery, but also silently disables the panel's main job and fights the user's stated intent | Recommend **off by default**, configurable. It is a real convenience and a real hazard; the hazard is why it cannot be the default |
| **Q19** | **GPIO 4 is wanted by two hardware mods.** §7.12 wants it as a CAN force-USB strap; §7.13 option D wants it for LEDC on the AP3032 EN pin. GPIO 15 and 16 are also absent from the published pin budget and are not yet ruled in or out | Both mods are board changes. R52 has to come off for option D | Jochen. Dimming removes a permanent UX limit. The strap matters only if Q22 says the CH9102 is behind `USB_SEL`. Phase 1 looks at GPIO 15 and 16 before this is treated as a one-pin choice |
| **Q20** | Does the SD **file browser** (`/sd/list`, `/sd/download`) survive on this board? | §6.7 rejected giving `tpl_sd` a CS hook, which leaves no browser at all | Jochen. Options: (a) move the browser into the recorder service, which owns the card anyway; (b) revisit the hook, which looks better now that `tpl_ws7_sd_select()` is a typed named accessor rather than a bare function pointer; (c) accept no browser and rely on the on-panel log SAVE (§8.12). Recommend (b) — it is the smallest change and keeps one SD implementation |
| **Q21** | **Arduino core 3.3+ (IDF 5.3) or `framework = esp-idf`?** | §6.3/§6.4/§6.5 all need managed components, and `esp_lcd_rgb_panel` requires IDF v5.2+ — above PlatformIO's default Arduino core 2.0.17 (IDF 4.4.4), where **none** of this stack exists | Jochen, in phase 0–1. Recommend **Arduino 3.3+ with the component manager**: it clears v5.2 while keeping the Arduino-based `tpl_*` template. `esp-idf` is the fallback but needs a template audit. Until this is answered the display bring-up cannot start |
| **Q15** | If a second wall panel is ever added, who wins? | Both would write the same durable duty with no arbitration, and "last writer wins" would make the outcome depend on which person touched a screen last | Not blocking. Answer when a second panel exists; a last-writer-wins with an explicit badge (§7.9) is probably enough for a two-panel house, but it should be a decision rather than an accident |
| **Q22** | **What exactly does the board's mechanical UART switch switch?** §3.3 models one mux — FSUSB42UMX under CH422G IO5. The bench found a *physical* switch that silently kills the CH9102 console, which means either a second gate or the same gate with a manual override | Decides whether N19 (drive `USB_SEL` low first thing in `setup()`) is **sufficient**, or whether firmware can be defeated by a switch position the operator set months ago. Also decides the runbook wording and whether §7.12's strap (Q17) fixes anything | **Jochen, bench, phase 0.** Read the schematic against the switch's silkscreen, then verify by toggling it and watching whether `USB_SEL`/`IO5` changes state. Cheap to answer and it sits under R1, which is the project's recovery guarantee |
| **Q23** | Is the 16 MB part **always** 16 MB, or does the board ship in variants? | §3.1.1 measured one board. The whitepaper is now written against 16 MB, and a batch with 8 MB would silently cap the OTA slots | Jochen. The `esp_lcd`/display stack does not care, but the partition table does. Record the MAC of every panel added, not just this one |
| **Q24** | **How does a living-room humidity sensor attach?** An AHT20's only address is `0x38`, which is CH422G WR-IO. A BMP280 at `0x76` or `0x77` does not collide (§7.8, R30) | F15 cannot be built as written. Putting the AHT20 on H1 fights every expander write | Jochen. Options: a humidity part on a free address; a second I²C bus on GPIOs phase 1 confirms are free (15 and 16 are the candidates, not a decision); or drop humidity and keep the BMP280 for pressure only. Until then the zone renders `—` |

## 13. Implementation Plan

Each phase ends in something that builds and runs. Phases 1-3 are Deliverable A and
are usable by any future project; 4-8 are Deliverable B.

| Phase | Content | Exit criterion |
| ----- | ------- | -------------- |
| **0** | This white paper. The rev 1.0 review is incorporated here; `docs/review.md` is not a second source | Every open question in §12 has an owner. Q21 and Q22 are answered or explicitly deferred before phase 2 |
| **1** | `tpl_waveshare_lcd7`: CH422G bring-up, LCD reset, backlight, UART0 check. **Measure CH422G IO5 with the ESP32 held in reset**, before the driver's first write (§3.3). Measure H2 (Q13). Answer Q22 by toggling the mechanical switch. Confirm GPIO 15 and 16 against the module pinout (Q19) | Panel lights; `Serial` is UART0; project 26 still builds (`pio run -e esp32ota`) after the new `.cpp` exists; IO5's power-on level is a measurement; H1 is 3.3 V |
| **2** | `esp_lcd_rgb_panel` + LVGL display in all four rotations, with the rotation assert (R4) | Coloured test pattern, correct in every orientation |
| **3** | GT911 touch in portrait; corner-tap calibration test (R14) | All four corners and all three zones hit reliably |
| **4** | Project skeleton, `platformio.ini` including `memory_type = qio_opi` and the USB flags, own `lv_conf.h`, a 16 MB partition table that includes `phy_init`, OTA confirmed on hardware | OTA updates a running build over WiFi. PSRAM size reports 8 MB |
| **5** | `tpl_debug_log` → PSRAM byte ring, detachable serial, WebSocket sink, `/log` serving the ring, `tools/tail_ws.py` | 1 MB ring fills and wraps; `tail_ws.py` shows live records on the Mac; `tpl_log_set_serial(false)` does not hang the system |
| **6** | `haus_state`, CAN task, `haus_can.h` decoding, **ROM-identity role resolution (§7.7)**, host harness stubs | Home screen shows live values from a replayed frame log, on macOS, no hardware; a simulated rank shift is reported, not silently relabelled |
| **7** | Home screen: background image, clock, vector heater graphic, power readout, status strip, zones | Runs on the panel; measured against the §10.3 budget |
| **8** | Recorder service: SPI backend, host backend, rotation, retention, CRC | A month of history appears on the SD card and survives a power cut mid-write |
| **9** | Detail screens + envelope charts at 1 day / 1 week / 1 month + scrubber | All four heater/power screens, all three ranges, on hardware and on macOS |
| **10** | **Log screen** (§8.12): monospace windowed view, follow-at-end, level filters, SD dump | Readable with WiFi down and USB disabled; a 5 000-line burst does not make the panel unresponsive |
| **11** | `tpl_can_monitor` extraction from project 26; gear screen | Project 26 still builds and passes; the gear opens the scope |
| **12** | `/ui.json` loader: theme, strings, units, static-screen order, with compiled-in fallback (§4.4) | A renamed zone and a changed accent colour take effect from an HTTP edit, no rebuild; a corrupt `/ui.json` still boots the hardcoded UI |
| **13** | **Local I²C sensors** (§7.8): bus-wide mutex, `sensorTask`, BMP280 at `0x76`/`0x77` with chip-id and the `0x88` calibration block. Humidity only if Q24 has a non-colliding part | A BMP280 reads on H1; an address scan does not show a device at `0x38` other than the expander's WR-IO behaviour; a pulled SDA pin degrades the living-room screen only (N14) |
| **14** | **Living-room detail screen** (§8.13) and the fifth home-screen zone | All three quantities chart at all three ranges; a missing sensor renders `—` with the reason |
| **15** | **PWM duty control** (§7.9, §8.6.1): TX path for `0x240`, slider, confirmation modal, `0x280` reconciliation, `PWM_SRC_*` badge | A drag sends nothing; only APPLY transmits; a duty is never shown as in force until `0x280` reports it; a conflicting writer is shown, not fought |
| **16** | **Three-tier history store** (§7.10): L1 day buffer, L2 hour rollup, boot restore, midnight rollover with two writes | A month of week/month charts renders with the SD card **removed** after boot; a power cut mid-day loses hours, not the month |
| **17** | **SML decode** (§7.4): lift `sml_publish()` from project 15 into `src/app/`, add `libsml`, 3-buffer reassembly with loss counters | The panel shows household watts from `1-0:16.7.0/255`; **project 15 still builds and shows the same value** (R26) |
| **18** | **Type scale** (§8.1.1): generate the 21 px body set, resize the log screen and CAN rows, add the build-time minimum-font sweep | No `lv_font` below 21 px exists; the log screen wraps at 38 chars and shows ~26 lines; the sweep fails the build if one does |
| **19** | **Bottom nav pane** (§8.11) and the Settings screen (§8.14) | All eight screens reachable in one tap from anywhere; zones still work as the secondary route |
| **20** | **CAN enable switch** (§7.12): boot sequence, config persistence, settings modal, actual-state readback | With CAN off, the CH9102 console stays up and CAN is idle. With CAN on, frames flow. Whether the console dies in that state is recorded against Q22, not assumed. Toggling survives a reboot |
| **21** | **Water model** (§7.11): `WaterModel` interface, null default, `TEMP_MODELLED` flag, dashed rendering, statistic exclusion | With no model installed the zone is `—`; with one, every surface labels it and no statistic consumes it |
| **22** | Polish: idle return, dither/glow assets, soak test | Runs 72 h without a watchdog reset, a leak, or a `heap_caps` drop over 8 KB |

Sequencing note: **phase 6 before phase 7** is deliberate. Building the host harness
against a stub decoder first means the entire GUI, including every later screen and
all the chart maths, is developed without flashing. That is the difference between
a two-week build and a two-month one.

Second sequencing note: **phase 5 before phase 7.** Bringing up the log ring before
the GUI means that if the display fights back — and an RGB panel at 800 × 480 will —
there is somewhere to read the error other than a USB console that is already gone.
This matters more now that the panel can write to the bus (R22): the log is the
record of who commanded what, so it has to exist before anything commands anything.

Third sequencing note: **phase 13 before phase 14, and phase 15 after the
diagnostics exist.** The climate sensors are read-only. The duty control is the
only feature that can change the house, so it waits until the display, the log
ring and the charts are already up (phases 5–11). It is not the last phase —
history, SML, the type scale and the nav pane follow it — but it is the first
phase that is allowed to transmit `0x240`. Landing it earlier would mean
debugging a UI bug and an actuator bug in the same session.

## 14. Appendix

### A. Related Documents

| Document | Location |
| -------- | -------- |
| This white paper | `ESP32/28_hauszentrale/docs/whitepaper.md` |
| Board reference, pinout, examples | `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/`. Prefer `source/waveshare_lcd_port.h` over the README's CH422G table |
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
| D3 | Two line buffers in internal RAM, `DIRECT` render mode | Full-screen PSRAM buffer | PSRAM is not DMA-capable here. A full frame is 24 ms of active pixels (§10.3); full-screen invalidation is what stalls, not the partial regions |
| D4 | Background PNG in SPIFFS, decoded once to static PSRAM | Embed the raw RGB565 blob | 768 KB against a 3 MB shared app slot; PNG is ~120 KB and re-encodes cheaply |
| D5 | Vector heater via `lv_canvas` | A bitmap with baked-in colours | The flame and pipes must react to live values; a bitmap can only go stale (§8.4) |
| D6 | Invisible `lv_obj_t` overlay buttons as zones | Coordinate tests in the read callback | Independent of the drawing; LVGL handles press, click and focus (§8.5) |
| D7 | Fixed-size records, now 24 B | Text CSV, as `15_power_log_eink` uses | Enables `seek()` — a month window is a seek, not a scan (§7.4, §9.5) |
| D8 | Streaming downsample to a per-pixel envelope | Cache the whole month in PSRAM | Bounds RAM at ~24 KB instead of ~1 MB, and keeps N6 true (R12) |
| D9 | Gap markers rather than interpolation | Connecting points across a dropout | Asserting measurements that were never taken is the failure mode a history view must not have (R6) |
| D10 | `AppState` snapshot + sequence counter | Direct global reads from the UI | Makes the threading contract explicit and keeps the host build honest (N5, §10.2) |
| D11 | Recorder owns writes, `tpl_sd` owns reads, flush-barrier between | Extend `tpl_sd` with a write path | `tpl_sd` is read-only by design and its CS model does not fit the CH422G (§6.7, R7) |
| D12 | Hand-authored LVGL for all screens | EEZ Studio | EEZ cannot express the vector heater or an envelope chart, and project 26 already left it (Q8) |
| D13 | Extend the existing `tpl_debug_log` | A new project-local logging module | It already exists, is opt-in, and is the natural home; extending it fixes it for every future console-less board (§7.6) |
| D14 | 1 MB **PSRAM byte ring**, newline-delimited | The current fixed 100 × 160 B internal-RAM line array | PSRAM is there and unused by anything else; a byte ring wastes nothing on short lines and truncates nothing important |
| D15 | Three sinks — ring (authoritative), Serial, WebSocket, panel | Writing to the console as the primary path | One copy, derived views. The CAN/USB conflict then stops being a debugging problem at all |
| D16 | WebSocket drained **from** the existing WS task, 8 KB/tick cap | Pushing to sockets from the log path | Decouples logging from a client's TCP window; zero cost when no client is attached (R2c) |
| D17 | Follow-at-end on by default, **disabled by any manual scroll** | Always following | Reading something must not be interrupted by the log yanking the view away; this is what every log viewer does and it is why people trust them |
| D18 | Log rendered as a windowed slice (~26 lines at the 4 mm floor, §8.1.1), rebuilt at 5 Hz | Appending to one growing `lv_label` | 1 MB of text in a label is an invalidation backlog that stalls `lv_task_handler` — the same trap `26_CYD_CAN_monitor` documented |
| D19 | Resolve `slot → rank → sensor ID → function`; learn rank→ROM from the identity frames, expire it, keep ROM→function in editable SPIFFS config | Caching "rank 1 = water in", or hardcoding ROMs in firmware | Rank is a derived address that shifts on unplug; functions belong to the sensor (R15). Hardcoded ROMs would make the panel correct on exactly one machine |
| D20 | Third zone shows `—` until a water-out sensor exists | Displaying rank 2's board temperature as "water out" | Rank 2 measures the transmitter PCB, not water. A plausible wrong number is worse than an honest gap (R16, Q9) |
| D21 | Long-press the status strip opens the log screen | Keeping colour inversion there; burying the log behind the gear | When serial is gone and the network is suspect, the log is the first thing one reaches for |
| D22 | Climate sensors on **H1**, explicitly not J8 | Wiring to the "Sensor AD" header, which is an analog input and cannot carry I²C | Both sensors are I²C parts; the schematic block is titled "Sensor AD" and its third pin is net-named `AD` (§3.5) |
| D23 | I²C sensors bypass the §7.7 rank/ROM resolution chain entirely | Reusing `sensor_roles[]` and the expiry logic for them | An I²C device has a fixed address and is physically where it is; it cannot suffer the failure mode that chain exists to handle, and routing it through would add a permanent "unbound" state to a sensor that is always bound (§7.8) |
| D24 | Separate sentinel for climate fields (`CLIMATE_VALID`) rather than reusing `INT16_MIN` | One sentinel for every temperature | A missing *probe* and a missing *I²C device* are different faults; conflating them on a chart would make a wiring problem look like a cold room (§7.4) |
| D25 | AHT20 is authoritative for temperature, BMP280 for pressure only | Showing both chips' temperatures | They disagree by a few tenths in a real room; showing both presents a normal disagreement as a fault (§7.8) |
| D26 | Heater and pump are **one** control point on the existing `0x240` | Adding a second actuator ID and a protocol change | `haus_can.h` defines no pump identifier; one duty output drives both, so the project needs no protocol change at all (§7.9) |
| D27 | Slider is 0 = OFF → 1000 = FULL, and is a *request*, never a report | Labelling the left end 1000; showing the sent value as in force | `pwm_out.cpp:124` drives the pin low at permille 0, so the other way round would command full power where a person reads "off"; and conflating sent with reported would put a number on the wall that no actuator honours (§7.9, §8.6.1) |
| D28 | Only the confirmation button may transmit | Drag-release, gesture, long-press, auto-repeat | A gesture is the one input a user cannot see they are giving, and the duty is durable across a power cut with no dead-man timeout (F19, N16, R21) |
| D29 | No ramp, no clamp, no dead-man timeout in the panel | Adding them "for safety" | Every one of them belongs to `AVR/01_heizung_temp`, which is the only place they protect *every* controller from rather than just this one. A panel-side interlock is an interlock that can be bypassed by the other controller, which makes it worse than none — it looks like a guarantee (§7.9, N15, R20, Q12) |
| D30 | Show `PWM_SRC_*` provenance as a first-class UI element | Showing only the duty value | A restored-from-EEPROM duty and a live commanded one are the same number and completely different situations; `haus_can.h:242` builds the distinction into the protocol, so hiding it wastes the only signal that has it (F20) |
| D31 | Theme, strings, units and static-screen order live in `/ui.json`; logic stays hardcoded | Fully declarative screens; fully hardcoded screens | §4.4. The heater graphic, the envelope chart and the log window are not declarable, so a layout DSL that could express this UI would be a reimplementation of LVGL's draw pipeline — and one that could not would not be describing this application |
| D32 | A bad or absent `/ui.json` falls back to the compiled-in UI | Letting a layout file fail the boot | With CAN enabled there is no USB console, so a panel that will not boot on a bad file can only be recovered by pushing a new SPIFFS image over OTA. Same posture as the background PNG (§4.4.3, §8.3) |
| D33 | **Three-tier history store**: minute samples on SD by day, a 1 440-sample day buffer and a 744-column hourly rollup in PSRAM | Per-minute SD appends with on-query downsampling | Drops card writes from 1 440/day to 2, which is the difference between a card that lasts years and one that lasts months; makes week and month charts a ~1 ms rescale instead of a 500 ms card read, which in turn removes the async chart build, the LRU cache and the loading spinner entirely (§7.10, §9.5) |
| D34 | One **daily** file rather than one monthly | Monthly file, daily append | Makes the write atomic and the loss window exactly "since the last rollover" rather than "since the start of the month". Costs one file per month instead of one; that is 12 files a year |
| D35 | L2 is **hourly**, and the chart says so | Minute-resolution month view | 1 MB per query is the thing D33 removes; hourly columns give 720 points for a month, which is already finer than a 440 px chart can show, and the 1-day view — where minute detail matters — is served from L1 at full resolution |
| D36 | Household power from **SML**, decoder lifted from project 15 | Writing a new SML parser; using `0x2C0` | Project 15 already decodes `1-0:16.7.0/255` with `libsml`, so the work is a lift rather than a build. The `0x2C0` amp chain was derived from hardware that has since changed, so it is not a measurement (§7.4, Q1) |
| D37 | **4 mm is a hard floor**, enforced at build time | Per-screen exceptions for dense screens | The requirement is readability without glasses; obeying it cost the log screen 18 lines and the CAN monitor 3 rows, which is the honest price. Enforced rather than intended because LVGL fonts are flash-resident, so there was no budget argument for cheating (N17, R25) |
| D38 | **Bottom nav pane** with seven icon tabs | Rev 1.2's hidden gear in the footer plus tap-zones | One tap to any screen from anywhere is the accessibility win; the zones survive as the secondary, spatial route. Icons not labels, because seven 4 mm labels do not fit in 480 px (§8.11) |
| D39 | Tabs and the stack are **different operations** (`jump` vs `push`) | One navigation primitive | A tab is a place and resets depth; the water-out comparison link is a step forward and needs somewhere to go back to. Collapsing them makes "back" ambiguous, which is what the stack existed to prevent (F7 keeps the header Back button) |
| D40 | Log screen is **full-bleed, no pane** | Pane everywhere for consistency | The 88 px buys eight more lines on the one screen where density is the point. The one deliberate exception to the navigation model, and the reason Back stays in its toolbar |
| D41 | Modelled water-out is **shown, permanently labelled** | `—` until a real sensor exists (rev 1.2) | The probes are slow, so a model is more useful than nothing — but only if it can never be mistaken for a measurement. Hence the flag bit, the dashed line, the badge, and exclusion from every statistic (§7.11, R24). A real sensor supersedes it automatically |
| D43 | Backlight dimming is **content-side**, not PWM on the expander | Chopping CH422G IO at 100 Hz | A CH422G output has no hardware PWM behind it, so it costs 100 I²C writes/s against the touch poll; the response is convex charge-transfer with a brightness floor; and the frequencies that dim well are the ones that flicker (§7.13). A clean LEDC fix exists but needs GPIO 4 (Q19) |
| D44 | Storage state is **evidence-based**, and *faulty* is distinct from *absent* | A single "SD ok / no SD" flag | A card that mounts and then fails to write loses history quietly, which is the exact failure the document's honesty rules exist to prevent. A sticky fault with a timestamp says when it happened (§8.15.1) |
| D46 | `tpl_waveshare_lcd7` is built **on `esp_io_expander`**, adding only a shadow, a mutex and named setters | Hand-rolled CH422G register writes | Espressif maintains the component, the Waveshare examples already use it, and it supplies the pin map, the all-or-nothing IO0–IO7 direction control, typed `enableOC_PushPull()` / `enableOC_OpenDrain()` and `multiSetIO()`. Rev 1.3's "must not go through `esp_io_expander`" was too strong — the component is the right foundation, and the per-pin read-modify-write cost is avoided *inside* it by batching (§6.5) |
| D47 | The CH422G bit map is taken from **the component's own documentation**, not the board README | The README's prose table | Three sources agree on IO1–IO5; the README is off by one against all three *and against its own register example*. Using the component's pin table means the numbers are the ones the library indexes by (§3.4) |
| D48 | The expander driver **writes** IO0–IO7 high on `reset()` / `begin()`. That is not a measured power-on level | Rev 1.0's "powers up low", and rev 1.4's reading of the same constants as silicon POR | `REG_WR_IO_DEFAULT_VAL` is the value `reset()` transmits. IO5 before that write is a phase-1 measurement. The CH9102 on GPIO 43/44 already took a ROM download (§3.1.1). Q22 decides whether CAN mode removes that path |
| D49 | The board's **flash is 16 MB**, and the partition table is sized for it | The 8 MB quoted in the identity table | Measured with `esptool flash-id`: Winbond `4018`, 16 MB, quad, 3.3 V. An 8 MB assumption caps the two OTA slots at half the part and hides 8 MB of usable board (§3.1.1, §7.1) |
| D50 | A **silent board is a switch position** until proven otherwise | Treating `No serial data received` as a dead board | The CH9102 enumerates, opens and returns zero bytes at every baud and every DTR/RTS combination when the switch is wrong — byte-identical to a dead chip. Five seconds of checking beats an hour of driver archaeology (§3.1.1, §7.5, Q22) |
| D51 | USB on this board is a **UART bridge**, so R3's UART0 requirement is a hardware fact, not a hope | Assuming the S3 might expose native CDC | No Espressif USB device (`0x303A`) is enumerated at any time, including in ROM download mode; the console arrives through a CH9102 on GPIO 43/44. Only the Arduino core's port choice remains open, so Q2 shrinks but does not close (§3.1.1) |
| D45 | Free space is shown **only when low** on the home screen, in full in Settings | A permanent capacity readout | A glanceable strip has no room for "37 GB free", but "12 MB free — floor 50 MB" is the single most useful thing to know about storage on a device that writes history (§8.15.3) |
| D42 | CAN enable **defaults off**, and N19 drives `USB_SEL` low before config is read | Defaulting on, for a panel whose job is the bus | The driver's init write would otherwise leave IO5 high. The ordering is the part firmware controls. Whether that is what keeps the CH9102 alive is Q22 (R27, §7.12) |

### D. Terminology

| Term | Meaning here |
| ---- | ------------ |
| Chamber | The firebox / combustion chamber temperature (`0x200`, slot per Q3) |
| Water in / water out | Flow temperature entering and leaving the heat exchanger |
| Gear | Bottom-right button opening the CAN monitor |
| Living room | The AHT20 + BMP280 on H1 — a second room, not part of the heater graphic |
| Duty / permille | Commanded PWM duty, 0–1000 **high time**. 0 = off, 1000 = full on |
| In force | The duty `0x280` last reported. The only value the UI ever calls "the duty" |
| Requested | Where the slider sits. A pending intent, never a measurement (§7.9) |
| Restored | `PWM_SRC_EEPROM` — the duty in force came from the AVR's restore point, with no live command behind it |
| Modelled | A value produced by the `WaterModel` function rather than measured. Always carries a `~` and never feeds a statistic (§7.11) |
| L0 / L1 / L2 | The three history tiers: samples on SD, the in-RAM day buffer, the in-RAM hourly rollup (§7.10) |
| Rollover | The midnight write of the day's samples and the rollup. Two SD writes per day, and the boundary of the 24-hour loss window |
| Nav pane | The persistent 88 px bar at the bottom of every screen except the log screen (§8.11) |
| 4 mm floor | 21 px on this panel. The smallest glyph the project will render, anywhere (§8.1.1) |
| Faulty (storage) | Mounted, but a rollover write failed, a CRC did not verify, or free space fell under the floor. Sticky, with a timestamp. Not the same as *absent* (§8.15.1) |
| Floor (storage) | 50 MB free, below which retention drops to 3 months and the SD dot turns amber (§9.7) |
| Sensor ID | The DS18B20 ROM. Unique and permanent; a temperature's function belongs to it |
| Rank | Derived position in the sorted list of *present* sensors. A transient address that shifts on unplug |
| Log screen | Full-screen console; long-press the status strip (§8.12) |
| Column | One pixel column of a chart, holding min/mean/max for that time slice |
| Envelope | The min–max band drawn per column, as opposed to a single mean line |
| `USB_SEL` | CH422G **IO5**. HIGH connects GPIO 19/20 to the CAN transceiver. The driver writes HIGH on init. The CH9102 console is GPIO 43/44 and is not the same path unless Q22 says so (§3.3) |

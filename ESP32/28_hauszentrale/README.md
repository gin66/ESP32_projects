# 28_hauszentrale — Waveshare ESP32-S3-Touch-LCD-7 panel

House panel for the heater CAN bus: RGB display, GT911 touch, three-tier
history, charts and one actuator command (`0x240`). The design and decisions are
in [`docs/whitepaper.md`](docs/whitepaper.md); the board abstraction is
[`prj_template/docs/waveshare_lcd7_board_abstraction.md`](../prj_template/docs/waveshare_lcd7_board_abstraction.md).

## Build

Requires PlatformIO (pioarduino platform `55.03.312-1`, Arduino core 3.3.12,
ESP-IDF 5.5.5). Secrets live outside the repo in `~/src/.private/` and are
symlinked by `make links` from `ESP32/`.

```bash
cd ESP32/28_hauszentrale

# OTA image (default)
pio run -e esp32ota

# USB image
pio run -e esp32usb

# SPIFFS filesystem
pio run -e esp32ota --target uploadfs
```

There is no project-specific config file: build options are in
`platformio.ini` as `-D` flags.

## Flash over OTA (normal path)

The panel serves the web updater at `http://hauszentrale.local/update` (fallback
IP: `192.168.1.146`). ArduinoOTA on port 3232 is not used on this board.

```bash
pio run -e esp32ota
curl -s --max-time 90 \
  -F "firmware=@.pio/build/esp32ota/firmware.bin" \
  http://hauszentrale.local/update
sleep 20
curl -s http://hauszentrale.local/version
```

A successful update returns the new git hash from `/version`. The panel reboots
into the new image; the settings and history survive.

## Flash over USB (recovery)

1. Put the board's mechanical UART switch in the **ESP32** position.
2. Connect the CH9102 USB port.
3. Hold **BOOT**, tap **RESET**, release **BOOT**.
4. Flash:

```bash
pio run -e esp32usb --target upload
pio device monitor
```

The CH9102 is UART0 on GPIO 43/44. `USB_SEL` (CH422G IO5) only switches GPIO
19/20 between native USB and the CAN transceiver; it does not gate the CH9102.
If `esptool` reports `No serial data received`, check the mechanical switch
before anything else.

## Panel recovery

If the panel shows vertical stripes or a shifted image — most often after an
OTA — tap **SETTINGS → PANEL RESET**. It recreates the RGB panel from the
compile-time `TPL_WS7_*` timing macros and pulses the ST7262 reset through CH422G
IO3. The touch UI stays usable; history and configuration are not erased. If the
screen is too corrupted to reach Settings, power-cycle the panel. The full note
lives in
[`waveshare_lcd7_board_abstraction.md`](../prj_template/docs/waveshare_lcd7_board_abstraction.md)
§6.3.

The panel timing defaults are the proven `4/8/8` values. `28_hauszentrale` runs
the pixel clock at 12 MHz (`TPL_WS7_PCLK_HZ`) for roughly 29 Hz scanout; remove
that build flag to return to 16 MHz.

## Runtime notes

- CAN is controlled in `SETTINGS`; when enabled the CH9102 console still works.
- `GET /config/can` without `?on=1` disables CAN. Use `/can` for read-only
  telemetry.
- The PWM switch/slider is enabled only while the CAN status is green, sends one
  `0x240` frame per change from the HEATER detail page, and refuses 501–999.
- Water-out is modelled from the measured duty → off/Soll table and is always
  labelled `~`/`MODELLED`.

## Host harness

The UI is the same source as the firmware. To render the screens on macOS:

```bash
cd host_harness
cmake -S . -B build
cmake --build build -j4
./build/hauszentrale_host
```

It writes `output_*.png` for the home, detail, water, power, living, settings,
storage, CAN, log and chart screens.

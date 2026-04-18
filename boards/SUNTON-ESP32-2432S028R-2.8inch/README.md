# SUNTON ESP32-2432S028R (2.8" LCD TFT with Touch)

ESP32-WROOM-32 development board with 2.8" TFT LCD, resistive touch, SD card slot, RGB LED, speaker, and light sensor. The classic "CYD" (Cheap Yellow Display).

## Links

- AliExpress: https://de.aliexpress.com/item/1005007286667162.html
- Board definitions + GPIO mappings: https://github.com/rzeldent/platformio-espressif32-sunton
- LVGL driver for all SUNTON boards: https://github.com/rzeldent/esp32-smartdisplay

## Photos

![Back](back.png)

## Schematics

![LCM Schematic](schematic_lcm.jpg)

![MCU Schematic](schematic_mcu.jpg)

## Specifications

| Spec           | Detail                                             |
| -------------- | -------------------------------------------------- |
| MCU            | ESP32-WROOM-32 — Xtensa dual-core LX6, 240 MHz     |
| Flash          | 4 MB                                               |
| PSRAM          | None                                               |
| Wireless       | Wi-Fi 802.11 b/g/n, Bluetooth 4.2 + BLE            |
| USB            | Micro USB                                          |
| Display        | 2.8" TFT LCD, 240 x 320, SPI                       |
| Display Driver | ILI9341                                            |
| Touch          | XPT2046 resistive (SPI)                            |
| Audio          | FM8002A amplifier, speaker connector (JST 1.25 2p) |
| SD Card        | TF slot (SPI)                                      |
| RGB LED        | Common anode (active LOW)                          |
| Light Sensor   | CdS photoresistor (GT36516)                        |
| I2C Expansion  | 2x JST 1.0 4-pin connectors                        |
| Power/Serial   | JST 1.25 4-pin connector                           |
| Battery        | JST 1.25 2-pin connector                           |

### Variants

| Variant           | USB               | Touch                    | Display Driver |
| ----------------- | ----------------- | ------------------------ | -------------- |
| ESP32-2432S028R   | Micro USB         | XPT2046 (resistive, SPI) | ILI9341        |
| ESP32-2432S028Rv2 | USB-C             | XPT2046 (resistive, SPI) | ILI9341        |
| ESP32-2432S028Rv3 | USB-C + Micro USB | XPT2046 (resistive, SPI) | ST7789         |

## Pin Mapping — Display (ILI9341, SPI)

| Function      | GPIO |
| ------------- | ---- |
| SPI_MOSI      | 13   |
| SPI_MISO      | 12   |
| SPI_SCLK      | 14   |
| TFT_CS        | 15   |
| TFT_DC        | 2    |
| TFT_BACKLIGHT | 21   |
| TFT_RST       | N/A  |

## Pin Mapping — Touch (XPT2046, SPI)

| Function  | GPIO |
| --------- | ---- |
| SPI_MOSI  | 32   |
| SPI_MISO  | 39   |
| SPI_SCLK  | 25   |
| TOUCH_CS  | 33   |
| TOUCH_INT | 36   |

Note: Touch uses a **separate SPI bus** (SPI3_HOST) from the display.

## Pin Mapping — SD Card (SPI)

| Function | GPIO |
| -------- | ---- |
| SPI_MOSI | 23   |
| SPI_MISO | 19   |
| SPI_SCLK | 18   |
| SD_CS    | 5    |

## Pin Mapping — Onboard Peripherals

| Function           | GPIO | Notes        |
| ------------------ | ---- | ------------ |
| RGB_LED_R          | 4    | Active LOW   |
| RGB_LED_G          | 16   | Active LOW   |
| RGB_LED_B          | 17   | Active LOW   |
| SPEAKER            | 26   | DAC2 / I2S   |
| CDS (light sensor) | 34   | Analog input |
| BOOT button        | 0    |              |

## PlatformIO

```ini
[env:esp32-2432S028r]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.partitions = default.csv
build_flags =
  -DARDUINO_ESP32_DEV
```

For the smartdisplay LVGL driver, add the board definition repository as a git submodule in `<project>/boards/` and use the [esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) library.

## Notes

- The RGB LED is active LOW — set GPIO LOW to turn on, HIGH to turn off.
- The touch SPI bus (GPIO 32/39/25/33) is separate from the display SPI bus (GPIO 13/12/14/15).
- The SD card uses yet another SPI bus (GPIO 23/19/18/5).
- For audio via I2S, use GPIO26 as DAC2 (left channel only). GPIO25 is connected to GT911 touch on some boards.
- The v3 variant uses an ST7789 display driver instead of ILI9341 (check markings on the box).
- The additional flash chip (W25Q32JV) is not always mounted on the board.

# Espressif ESP32-C6-DevKitC-1

Official entry-level development board based on ESP32-C6-WROOM-1 with Wi-Fi 6, Bluetooth 5, Zigbee 3.0, and Thread 1.3.

## Links

- Official page: https://www.espressif.com/en/dev-board/esp32-c6-devkitc-1
- User guide: https://docs.espressif.com/projects/espressif-esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html
- ESP32-C6-WROOM-1 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_datasheet_en.pdf
- Schematic (v1.4): https://docs.espressif.com/_static/esp32-c6-devkitc-1-schematic-v1.4.pdf

## Photos

![Board](photo.png)

![Annotated Front](annotated_front.png)

## Block Diagram

![Block Diagram](block_diagram.png)

## Pinout

![Pinout](pinout.png)

## Specifications

| Spec                | Detail                                                                                 |
| ------------------- | -------------------------------------------------------------------------------------- |
| Module              | ESP32-C6-WROOM-1 (PCB antenna) or ESP32-C6-WROOM-1U (external antenna)                 |
| MCU                 | ESP32-C6 — RISC-V 32-bit single-core, up to 160 MHz                                    |
| Flash               | 8 MB SPI                                                                               |
| PSRAM               | None                                                                                   |
| Wireless            | Wi-Fi 6 (802.11ax, 2.4 GHz), Bluetooth 5 (LE), IEEE 802.15.4 (Zigbee 3.0 + Thread 1.3) |
| USB                 | 2x USB Type-C (UART bridge + native USB)                                               |
| USB-UART Bridge     | CP210x (up to 3 Mbps)                                                                  |
| RGB LED             | WS2812 addressable, GPIO8                                                              |
| Buttons             | BOOT, RESET                                                                            |
| Power               | 5V via USB or pin header, 3.3V via LDO                                                 |
| Current Measurement | J5 jumper header                                                                       |
| Board Version       | v1.2 (latest)                                                                          |

## Pin Header Map (v1.2)

| Header | GPIO               | Function                   |
| ------ | ------------------ | -------------------------- |
| J1-1   | 3V3                | Power output               |
| J1-2   | IO0                | BOOT button                |
| J1-3   | IO1                |                            |
| J1-4   | IO2                |                            |
| J1-5   | IO3                | STRAP: must be LOW at boot |
| J1-6   | IO4                |                            |
| J1-7   | IO5                |                            |
| J1-8   | IO6                |                            |
| J1-9   | IO7                |                            |
| J1-10  | GND                | Ground                     |
| J2-1   | 5V                 | Power input/output         |
| J2-2   | IO8                | RGB LED (WS2812)           |
| J2-3   | IO9                |                            |
| J2-4   | IO10               |                            |
| J2-5   | IO11               |                            |
| J2-6   | IO12               |                            |
| J2-7   | IO13               |                            |
| J2-8   | IO14               |                            |
| J2-9   | IO15               |                            |
| J2-10  | GND                | Ground                     |
| J3-1   | 3V3                | Power output               |
| J3-2   | IO16               |                            |
| J3-3   | IO17               |                            |
| J3-4   | IO18               |                            |
| J3-5   | IO19               |                            |
| J3-6   | IO20               |                            |
| J3-7   | IO21               |                            |
| J3-8   | IO22               |                            |
| J3-9   | IO23               | STRAP: must be LOW at boot |
| J3-10  | GND                | Ground                     |
| J4-1   | 5V                 | Power input/output         |
| J4-2   | IO24               |                            |
| J4-3   | IO25               |                            |
| J4-4   | IO26               |                            |
| J4-5   | IO27               |                            |
| J4-6   | IO28               |                            |
| J4-7   | IO29               |                            |
| J4-8   | IO30               |                            |
| J4-9   | TX (IO16 via UART) | UART0 TX                   |
| J4-10  | RX (IO17 via UART) | UART0 RX                   |

Note: GPIO0–5, 23, 24, 25, 26 are used for the SPI flash bus (not user-accessible).

## Peripherals

- 1x SPI (GP-SPI)
- 1x I2C
- 2x UART
- 1x I2S
- 1x TWAI (CAN)
- 12-bit ADC (7 channels)
- LED PWM (6 channels)
- USB Serial/JTAG
- IEEE 802.15.4 radio (Zigbee/Thread)

## PlatformIO

```ini
[env:esp32-c6-devkitc-1]
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
```

## Notes

- Two USB-C ports: one via UART bridge (for flashing/serial), one native ESP32-C6 USB (for JTAG/debug).
- GPIO3 and GPIO23 are strapping pins — must be LOW during boot.
- The RGB LED is a WS2812 addressable LED on GPIO8.
- Supports Wi-Fi 6 features: OFDMA, MU-MIMO, TWT (Target Wake Time).

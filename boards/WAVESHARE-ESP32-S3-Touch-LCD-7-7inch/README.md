# Waveshare ESP32-S3-Touch-LCD-7 (7" Capacitive Touch LCD)

ESP32-S3 development board with 7-inch capacitive touch LCD (800×480), CAN bus, RS485, SD card slot, and CH422G IO expander. Uses **RGB display interface** (not SPI like the CYD boards), making it fundamentally different from the prj_template display abstractions.

## Links

- Product page: https://www.waveshare.com/esp32-s3-touch-lcd-7.htm
- Documentation: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7
- Schematic: [ESP32-S3-Touch-LCD-7-Sch.pdf](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf)
- GitHub examples: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
- CH422G datasheet: https://files.waveshare.com/wiki/common/CH422DS1_EN.pdf
- ST7262 datasheet: https://files.waveshare.com/wiki/common/ST7262.pdf
- GT911 datasheet: https://files.waveshare.com/wiki/common/GT911_EN_Datasheet.pdf
- Community projects: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7/#community-showcase

## Specifications

| Spec              | Detail                                             |
| ----------------- | -------------------------------------------------- |
| MCU               | ESP32-S3-WROOM-1U — Xtensa dual-core LX7, 240 MHz |
| Flash             | 8 MB                                               |
| PSRAM             | 8 MB Octal SPI                                   |
| Wireless          | Wi-Fi 802.11 b/g/n, Bluetooth 5 + BLE              |
| USB               | USB Type-C (UART download + OTG)                   |
| Display           | 7" TFT LCD, 800×480, **RGB parallel interface**    |
| Display Driver    | ST7262                                             |
| Touch             | GT911 capacitive (I2C, up to 5 points)             |
| IO Expander       | CH422G (12 GPIOs via I2C)                          |
| CAN               | TJA1051 transceiver (via FSUSB42UMX analog switch) |
| RS485             | Via USB-to-RS485 converter board                   |
| SD Card           | TF slot (SPI, CS controlled by CH422G)             |
| Battery           | PH2.0 connector, single-cell 3.7V ≤ 2000 mAh       |
| Termination       | 120 Ω CAN/RS485 resistors (jumper selectable)      |
| Antenna           | PCB antenna (keep clear)                           |
| Connector         | PH2.0 connectors for ADC, CAN, I2C, RS485 pins    |

## Pinout / Schematics

### RGB Display — ST7262 (16-bit parallel RGB)

| Function          | GPIO |
| ----------------- | ---- |
| HSYNC             | 46   |
| VSYNC             | 3    |
| DE                | 5    |
| PCLK              | 7    |
| DATA0             | 14   |
| DATA1             | 38   |
| DATA2             | 18   |
| DATA3             | 17   |
| DATA4             | 10   |
| DATA5             | 39   |
| DATA6             | 0    |
| DATA7             | 45   |
| DATA8             | 48   |
| DATA9             | 47   |
| DATA10            | 21   |
| DATA11            | 1    |
| DATA12            | 2    |
| DATA13            | 42   |
| DATA14            | 41   |
| DATA15            | 40   |

**RGB Timings:**
| Parameter         | Value |
| ----------------- | ----- |
| Pixel clock       | 16 MHz |
| H_res             | 800   |
| V_res             | 480   |
| HSync PW          | 4     |
| HSync Back Porch  | 8     |
| HSync Front Porch | 8     |
| VSync PW          | 4     |
| VSync Back Porch  | 8     |
| VSync Front Porch | 8     |

### Touch — GT911 (I2C)

| Function  | GPIO |
| --------- | ---- |
| I2C_SDA   | 8    |
| I2C_SCL   | 9    |

### CH422G IO Expander (I2C, address 0x24)

The CH422G controls **all** peripheral routing and reset signals. It is connected to the ESP32-S3 via I2C:

| Function          | CH422G Pin | GPIO (via CH422G) | Notes |
| ----------------- | ---------- | ------------------- | ----- |
| IO0               | IO0        | GPIO 1  | **TP_RST** — Touch panel reset |
| IO1               | IO1        | GPIO 2  | **LCD_BL** — LCD backlight enable |
| IO2               | IO2        | GPIO 3  | **LCD_RST** — LCD panel reset |
| IO3               | IO3        | GPIO 4  | **SD_CS** — SD card chip select (active LOW) |
| IO4               | IO4        | GPIO 5  | **USB_SEL** — CAN/USB analog switch control |
| IO5               | IO5        | GPIO 6  | — |
| IO6               | IO6        | GPIO 7  | — |
| IO7               | IO7        | GPIO 8  | — |
| OC0               | OC0        | GPIO 9  | — |
| OC1               | OC1        | GPIO 10 | — |
| OC2               | OC2        | GPIO 11 | — |
| OC3               | OC3        | GPIO 12 | — |

**I2C Bus:** SDA=GPIO 8, SCL=GPIO 9, 400 kHz

**⚠️ I2C Address Map** (CH422G + GT911 share the bus):

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
10:  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
20:  20 21 22 23 24 25 26 27 -  -  -  -  -  -  -  -
30:  30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f
40:  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
50:  -  -  -  -  -  -  -  -  -  -  -  -  -  -  5d -
60:  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
70:  -
```

### CAN Bus — TJA1051 (via FSUSB42UMX analog switch)

| Function | GPIO | Notes |
| -------- | ---- | ----- |
| TX       | 19   | **Only when USB_SEL = HIGH** |
| RX       | 20   | **Only when USB_SEL = HIGH** |

**⚠️ CAN/USB Switch — FSUSB42UMX (IO Extender Controlled)**

This is the **critical peculiarity** of this board:

1. The CAN bus (TX/RX on GPIO 19/20) is connected to the ESP32-S3 through a **FSUSB42UMX** USB/signal analog switch chip.
2. The switch is controlled by **CH422G IO4 (GPIO 5)** via the `USB_SEL` signal:
   - **USB_SEL = LOW (default):** FSUSB42UMX is **disabled** — GPIO 19/20 are **disconnected** from CAN. USB serial works normally.
   - **USB_SEL = HIGH:** FSUSB42UMX is **enabled** — GPIO 19/20 are connected to CAN TX/RX. **USB serial is disabled** (cannot use USB for programming/monitoring).

**To use CAN:**
```cpp
// Write HIGH to CH422G IO4 (USB_SEL pin) to enable CAN:
// Via direct I2C register write:
i2c_master_write_to_device(I2C_NUM_0, 0x24, &enable_buf, 1, portMAX_DELAY);  // Set CH422G to output mode
i2c_master_write_to_device(I2C_NUM_0, 0x38, &usb_sel_high, 1, portMAX_DELAY); // Set USB_SEL = HIGH
// usb_sel_high = 0x20 (bit 5 set in WR-IO register at address 0x38)
```

**To use USB serial again:**
```cpp
// Write LOW to CH422G IO4 (USB_SEL pin) to disable CAN, re-enable USB:
i2c_master_write_to_device(I2C_NUM_0, 0x24, &enable_buf, 1, portMAX_DELAY);  // Set CH422G to output mode
i2c_master_write_to_device(I2C_NUM_0, 0x38, &usb_sel_low, 1, portMAX_DELAY); // Set USB_SEL = LOW
// usb_sel_low = 0x10 (bit 5 clear in WR-IO register at address 0x38)
```

**⚠️ Important:** Once USB_SEL is set HIGH, you **cannot** use USB serial for debugging or OTA. You must set it LOW again before reprogramming.

### SD Card (SPI, CS via CH422G)

| Function | GPIO (ESP32) | Notes |
| -------- | ------------ | ----- |
| MOSI     | 11           |     |
| MISO     | 13           |     |
| SCLK     | 12           |     |
| CS       | CH422G IO3   | GPIO 4 — Must be pulled LOW by CH422G to enable |

**SD Card Init sequence:**
```cpp
// 1. Set CH422G to output mode (write 0x01 to WR-SET at 0x24):
i2c_master_write_to_device(I2C_NUM_0, 0x24, &0x01, 1, portMAX_DELAY);
// 2. Pull SD_CS LOW (write 0x0A to WR-IO at 0x38):
i2c_master_write_to_device(I2C_NUM_0, 0x38, &0x0A, 1, portMAX_DELAY);
// 3. Now SD card SPI is accessible on GPIO 11/12/13
```

### Backlight Control

| Function  | CH422G Pin | Notes |
| --------- | ---------- | ----- |
| LCD_BL    | IO1        | GPIO 2 — Write HIGH to enable backlight |

Backlight enable sequence:
```cpp
i2c_master_write_to_device(I2C_NUM_0, 0x24, &0x01, 1, portMAX_DELAY);  // Output mode
i2c_master_write_to_device(I2C_NUM_0, 0x38, &0x1E, 1, portMAX_DELAY);  // LCD_BL = HIGH (bit 1)
// To disable: 0x16 (bit 1 clear)
```

### Touch Reset (via CH422G)

| Function  | CH422G Pin | GPIO |
| --------- | ---------- | ---- |
| TP_RST    | IO0        | 1    |

Touch reset sequence:
```cpp
i2c_master_write_to_device(I2C_NUM_0, 0x24, &0x01, 1, portMAX_DELAY);  // Output mode
i2c_master_write_to_device(I2C_NUM_0, 0x38, &0x2C, 1, portMAX_DELAY);  // TP_RST = HIGH (bit 2)
// Then toggle: 0x2E to set, 0x20 to reset (bits 2+3)
```

### Onboard Peripherals

| Function          | GPIO / Notes |
| ----------------- | ------------ |
| BOOT button       | GPIO 0 (direct) |
| RESET button      | GPIO 0 (via circuit) |
| Battery connector | PH2.0, 3.7V single-cell ≤ 2000 mAh |
| USB Type-C        | UART download + data (when USB_SEL = LOW) |
| PH2.0 connectors  | Break out ADC, CAN, I2C, RS485 pins |
| CAN termination   | 120 Ω (jumper selectable, default connected) |

## PlatformIO Configuration

This board uses **RGB parallel interface** (not SPI), requiring a fundamentally different approach from CYD boards. No LVGL display abstraction layer like `tpl_display` will work directly — the RGB interface uses completely different GPIOs and timing.

```ini
[env:esp32-s3-touch-lcd-7]
platform = espressif32
board = esp32-s3-devkitc-1     ; or custom board definition
framework = arduino             ; or esp-idf
monitor_speed = 115200
board_build.partitions = default_8MB.csv
board_build.mcu = esp32s3
board_build.flash_size = 8MB
board_build.psram_size = 8MB
build_flags =
  -D CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911=1
  -D EXAMPLE_LCD_H_RES=800
  -D EXAMPLE_LCD_V_RES=480
  -D EXAMPLE_LCD_PIXEL_CLOCK_HZ=16000000
  -D EXAMPLE_RGB_DATA_WIDTH=16
  -D EXAMPLE_RGB_BIT_PER_PIXEL=16
  -D EXAMPLE_LCD_IO_RGB_HSYNC=46
  -D EXAMPLE_LCD_IO_RGB_VSYNC=3
  -D EXAMPLE_LCD_IO_RGB_DE=5
  -D EXAMPLE_LCD_IO_RGB_PCLK=7
  ; ... (all 16 data pins)
  -D I2C_MASTER_SDA_IO=8
  -D I2C_MASTER_SCL_IO=9
  ; CAN: GPIO 19/20 only active when USB_SEL=HIGH
```

### ESP-IDF MenuConfig Requirements

```
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_120M=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_SPIRAM_SPEED_120M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

## Integration with prj_template

### Key Differences from CYD Boards

| Feature         | CYD (2.8"/3.5")          | ESP32-S3-Touch-LCD-7     |
| --------------- | ------------------------ | ------------------------ |
| Display iface   | SPI                      | **RGB parallel (16-bit)** |
| Driver          | ILI9341 / ST7796         | **ST7262**               |
| Resolution      | 240×320 / 320×480        | **800×480**              |
| Touch           | I2C (GT911) / SPI (XPT2046) | **I2C (GT911)**    |
| IO Expander     | None                     | **CH422G (I2C, 0x24)**   |
| CAN             | Direct GPIO              | **Via FSUSB42UMX switch** |
| SD Card CS      | Direct GPIO              | **Via CH422G IO3**       |
| Backlight       | Direct GPIO (LEDC)       | **Via CH422G IO1**       |
| LVGL            | esp_lcd + TFT_eSPI       | **esp_lcd + RGB panel**  |

### New Template Module Needed: `tpl_waveshare_lcd7`

This board requires a dedicated template module (NOT compatible with `tpl_display`):

```cpp
// tpl_waveshare_lcd7.h
#ifndef tpl_waveview_lcd7_h
#define tpl_waveview_lcd7_h

#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_io_expander.h>

// I2C for CH422G
#define WS7_I2C_NUM I2C_NUM_0
#define WS7_I2C_SDA 8
#define WS7_I2C_SCL 9
#define WS7_CH422G_ADDR 0x24

// CH422G register addresses (7-bit >> 1)
#define WS7_CH422G_REG_WR_SET 0x12  // 0x48 >> 1
#define WS7_CH422G_REG_WR_OC  0x23  // 0x46 >> 1
#define WS7_CH422G_REG_WR_IO  0x38  // 0x70 >> 1
#define WS7_CH422G_REG_RD_IO  0x26  // 0x4D >> 1

// RGB Display pins (direct GPIO)
#define WS7_RGB_HSYNC  46
#define WS7_RGB_VSYNC  3
#define WS7_RGB_DE     5
#define WS7_RGB_PCLK   7
#define WS7_RGB_DATA0  14
#define WS7_RGB_DATA1  38
#define WS7_RGB_DATA2  18
#define WS7_RGB_DATA3  17
#define WS7_RGB_DATA4  10
#define WS7_RGB_DATA5  39
#define WS7_RGB_DATA6  0
#define WS7_RGB_DATA7  45
#define WS7_RGB_DATA8  48
#define WS7_RGB_DATA9  47
#define WS7_RGB_DATA10 21
#define WS7_RGB_DATA11 1
#define WS7_RGB_DATA12 2
#define WS7_RGB_DATA13 42
#define WS7_RGB_DATA14 41
#define WS7_RGB_DATA15 40

// SD card SPI pins (direct GPIO)
#define WS7_SD_MOSI 11
#define WS7_SD_MISO 13
#define WS7_SD_SCLK 12
// SD_CS controlled by CH422G IO3 (GPIO 4)

// CAN pins (only when USB_SEL = HIGH)
#define WS7_CAN_TX 19
#define WS7_CAN_RX 20

// CH422G control values for USB_SEL
#define WS7_USB_SEL_HIGH  0x20  // Set bit 5: enable CAN, disable USB
#define WS7_USB_SEL_LOW   0x10  // Clear bit 5: disable CAN, enable USB
#define WS7_OUTPUT_MODE   0x01  // Set CH422G to output mode

// CH422G values for peripheral control
#define WS7_TP_RST_HIGH   0x2C  // Touch reset HIGH
#define WS7_TP_RST_LOW    0x20  // Touch reset LOW
#define WS7_LCD_BL_ON     0x1E  // Backlight ON
#define WS7_LCD_BL_OFF    0x16  // Backlight OFF
#define WS7_SD_CS_ENABLE  0x0A  // SD card CS LOW (active)
#define WS7_SD_CS_DISABLE 0x06  // SD card CS HIGH (inactive)

// Display timing constants
#define WS7_LCD_H_RES   800
#define WS7_LCD_V_RES   480
#define WS7_PCLK_HZ     16000000
#define WS7_BOUNCE_LINES 10

// Function prototypes
esp_err_t tpl_waveview_lcd7_init(
    lv_display_t **out_disp,
    lv_indev_t **out_touch,
    uint32_t *draw_buf_size);

esp_err_t tpl_waveview_lcd7_backlight_on(void);
esp_err_t tpl_waveview_lcd7_can_enable(void);   // USB_SEL = HIGH
esp_err_t tpl_waveview_lcd7_can_disable(void);  // USB_SEL = LOW (default)
bool tpl_waveview_lcd7_is_can_enabled(void);

// SD card helper
esp_err_t tpl_waveview_lcd7_sd_cs_set(bool enabled);

#endif
```

### Usage Pattern (Arduino)

```cpp
#include <esp_io_expander.hpp>

// Initialize CH422G IO expander
esp_expander::CH422G *expander = new esp_expander::CH422G(9, 8, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS);
expander->init();
expander->begin();
expander->enableAllIO_Output();

// Enable CAN (disables USB serial!)
expander->digitalWrite(USB_SEL, HIGH);  // IO4 = HIGH

// Initialize CAN
twai_driver_install(...);
twai_start();

// --- Later, to re-enable USB serial for debugging: ---
expander->digitalWrite(USB_SEL, LOW);  // IO4 = LOW
// Now USB serial works again
```

### Usage Pattern (ESP-IDF)

```c
// Enable CAN (disables USB serial!)
uint8_t write_buf = 0x01;  // Set CH422G to output mode
i2c_master_write_to_device(I2C_NUM_0, 0x24, &write_buf, 1, portMAX_DELAY);
write_buf = 0x20;  // USB_SEL = HIGH
i2c_master_write_to_device(I2C_NUM_0, 0x38, &write_buf, 1, portMAX_DELAY);

// Initialize TWAI driver
twai_driver_install(&g_config, &t_config, &f_config);
twai_start();

// --- To re-enable USB: ---
write_buf = 0x10;  // USB_SEL = LOW
i2c_master_write_to_device(I2C_NUM_0, 0x38, &write_buf, 1, portMAX_DELAY);
```

## Community Projects

- **PiLab Panel** — Web-based scriptable HMI editor with MQTT: https://github.com/OpenPiLab/PiLab-Panel
- **Oscar 100 WB Monitor** — Ham radio satellite monitor: https://github.com/HB9IIU/ESP32_S3_WaveShare_OSCAR_WB_MONITOR
- **Satellite Tracker** — ESP32-S3 sat tracker: https://github.com/HB9IIU/ESP32_S3_INTEGRATED_DISPLAY_SAT_TRACKER
- **Plane Radar** — ADS-B plane radar: https://github.com/k4m454k/big_plane_radar
- **Aura AQ** — Smart air quality monitor: https://aura-aq.com/
- **Kitchen Dashboard** — ESPHome + LVGL + Home Assistant: https://www.youtube.com/watch?v=v1UE3SAf5vI
- **UADash** — Car dashboard UI: https://www.youtube.com/watch?v=nzx4fkk9hWo

## Notes

- **The 7" display occupies almost all GPIOs** — the CH422G IO expander is **mandatory** for all peripheral control.
- **CAN/USB is a hard trade-off** — you cannot use USB serial AND CAN simultaneously. Plan your debugging strategy accordingly (use Wi-Fi/OTA for debugging when CAN is needed).
- **SD card CS is controlled by CH422G** — you must set CH422G IO3 LOW to enable the SD card SPI bus.
- **Backlight is controlled by CH422G** — you must set CH422G IO1 HIGH to enable the display backlight.
- **Touch reset is controlled by CH422G** — GT911 reset uses CH422G IO0.
- **Performance:** Single-core LVGL benchmark: ~26 FPS average, ~41 FPS interface at 21 MHz PCLK.
- **PSRAM must be configured:** Octal mode, 120 MHz, matching flash frequency.
- **No `tpl_display` compatibility** — this board uses RGB parallel interface, completely different from CYD SPI displays. A new template module is required.
- **Default state on boot:** USB_SEL = LOW (USB works, CAN disabled). You must explicitly enable CAN.
- **120 Ω termination resistors** are connected by default — use jumpers to set to NC if needed.
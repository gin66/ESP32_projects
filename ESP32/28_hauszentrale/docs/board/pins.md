# Waveshare ESP32-S3-Touch-LCD-7 — Pin Mapping

Transcribed from `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/source/waveshare_lcd_port.h`.

## I2C Bus (CH422G Expander)

| Signal | GPIO | I2C Address (8-bit) | I2C Address (7-bit) | Register |
|--------|------|---------------------|---------------------|----------|
| SDA | 8 | 0x48 | 0x24 | WR-SET (config) |
| SCL | 9 | 0x46 | 0x23 | WR-OC (open-collector) |
| — | — | 0x70 | 0x38 | WR-IO (output data) |
| — | — | 0x4D | 0x26 | RD-IO (input read) |

## CH422G Output Assignments

| IO Pin | Function | tpl_ws7 Macro |
|--------|----------|---------------|
| 1 | Touch panel reset | `TPL_WS7_IO_TP_RST` |
| 2 | LCD backlight | `TPL_WS7_IO_LCD_BL` |
| 3 | LCD panel reset | `TPL_WS7_IO_LCD_RST` |
| 4 | SD card CS (active-LOW) | `TPL_WS7_IO_SD_CS` |
| 5 | USB/CAN select | `TPL_WS7_IO_USB_SEL` |

## RGB Panel (ST7262, 16-bit RGB565)

| Signal | GPIO |
|--------|------|
| VSYNC | 3 |
| HSYNC | 46 |
| DE | 5 |
| PCLK | 7 |
| DATA0 | 14 |
| DATA1 | 38 |
| DATA2 | 18 |
| DATA3 | 17 |
| DATA4 | 10 |
| DATA5 | 39 |
| DATA6 | 0 |
| DATA7 | 45 |
| DATA8 | 48 |
| DATA9 | 47 |
| DATA10 | 21 |
| DATA11 | 1 |
| DATA12 | 2 |
| DATA13 | 42 |
| DATA14 | 41 |
| DATA15 | 40 |

## RGB Timing

| Parameter | Value |
|-----------|-------|
| PCLK frequency | 16 MHz |
| Horizontal pulse width (HPW) | 4 |
| Horizontal back porch (HBP) | 8 |
| Horizontal front porch (HFP) | 8 |
| Vertical pulse width (VPW) | 4 |
| Vertical back porch (VBP) | 8 |
| Vertical front porch (VFP) | 8 |
| Bounce buffer size | 8000 (800 × 10) |

## SD Card SPI

| Signal | GPIO |
|--------|------|
| MOSI | 11 |
| SCLK | 12 |
| MISO | 13 |

## Display Specs

| Parameter | Value |
|-----------|-------|
| Model | ST7262 |
| Resolution | 800 × 480 |
| Color depth | 16-bit RGB565 |
| Data width | 16 bits |

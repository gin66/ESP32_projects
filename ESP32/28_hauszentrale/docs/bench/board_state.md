# Board State

Derived from S0.01 — mechanical UART switch identification.

## UART Switch

**Silkscreen label:** `SW2` (Waveshare ESP32-S3-Touch-LCD-7-7inch board)

**As-shipped position:** USB (connects CH9102 USB-to-UART to ESP32-S3 GPIO43/44)

**H2 fitted jumper position:** CAN (connects TJA1051 CAN transceiver to GPIO19/20)

When `SW2` is in CAN position, the CH422G expander's IO5 (USB_SEL) controls whether
GPIO19/20 route to the CAN transceiver or remain available for USB-Serial-JTAG.

## I2C Bus Voltage

All headers unplugged: SDA/SCL measured at 3.3 V (not 5 V). Q13 resolved — the CH422G
is powered from the 3.3 V rail.

## Backlight

**Status:** Constantly ON (hardware default).

The CH422G IO2 controls the AP3032 backlight enable pin. At power-on the CH422G
initializes with all outputs HIGH (IO_OE = 1), which turns the backlight on.

**Software fix:** `tpl_ws7_expander_init()` now turns IO2 LOW immediately after
initializing the I2C bus, disabling the backlight.

## References

- Board front image: `boards/WAVESHARE-ESP32-S3-Touch-LCD-7-7inch/images/board_front.jpg`
- Schematic: Waveshare ESP32-S3-Touch-LCD-7 schematic (sheet 4 — CH422G)

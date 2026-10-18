#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef IS_WAVESHARE_LCD7

#include <driver/spi_master.h>
#include <lvgl.h>

#define TPL_WS7_SCREEN_WIDTH 800
#define TPL_WS7_SCREEN_HEIGHT 480

// Logical orientation (see whitepaper §6.6). This is NOT the LVGL enum:
//   0 = portrait 480x800 (LVGL rotation 90)   1 = landscape 800x480 (LVGL
//   rotation 0) 2 = portrait 480x800 (LVGL rotation 270)  3 = landscape 800x480
//   (LVGL rotation 180)
#ifndef TPL_WS7_ROTATION
#define TPL_WS7_ROTATION 0
#endif

#if (TPL_WS7_ROTATION == 0) || (TPL_WS7_ROTATION == 2)
#define TPL_WS7_WIDTH 480
#define TPL_WS7_HEIGHT 800
#else
#define TPL_WS7_WIDTH 800
#define TPL_WS7_HEIGHT 480
#endif

// I2C bus for CH422G
#define TPL_WS7_I2C_SDA 8
#define TPL_WS7_I2C_SCL 9
#define TPL_WS7_I2C_ADDR 0x24

// CH422G IO assignments
#define TPL_WS7_IO_TP_RST 1
#define TPL_WS7_IO_LCD_BL 2
#define TPL_WS7_IO_LCD_RST 3
#define TPL_WS7_IO_SD_CS 4
#define TPL_WS7_IO_USB_SEL 5

// RGB panel pins
#define TPL_WS7_PIN_VSYNC 3
#define TPL_WS7_PIN_HSYNC 46
#define TPL_WS7_PIN_DE 5
#define TPL_WS7_PIN_PCLK 7
#define TPL_WS7_PIN_DATA0 14
#define TPL_WS7_PIN_DATA1 38
#define TPL_WS7_PIN_DATA2 18
#define TPL_WS7_PIN_DATA3 17
#define TPL_WS7_PIN_DATA4 10
#define TPL_WS7_PIN_DATA5 39
#define TPL_WS7_PIN_DATA6 0
#define TPL_WS7_PIN_DATA7 45
#define TPL_WS7_PIN_DATA8 48
#define TPL_WS7_PIN_DATA9 47
#define TPL_WS7_PIN_DATA10 21
#define TPL_WS7_PIN_DATA11 1
#define TPL_WS7_PIN_DATA12 2
#define TPL_WS7_PIN_DATA13 42
#define TPL_WS7_PIN_DATA14 41
#define TPL_WS7_PIN_DATA15 40

// SD SPI pins
#define TPL_WS7_SD_MOSI 11
#define TPL_WS7_SD_SCLK 12
#define TPL_WS7_SD_MISO 13

esp_err_t tpl_ws7_expander_init(void);

esp_err_t tpl_ws7_backlight(bool on);
esp_err_t tpl_ws7_touch_reset_assert(void);
esp_err_t tpl_ws7_touch_reset_release(void);
esp_err_t tpl_ws7_panel_reset(void);
esp_err_t tpl_ws7_sd_select(bool select);
esp_err_t tpl_ws7_set_usb_mode(bool usb);
bool tpl_ws7_can_available(void);

/* Read the CH422G RD-IO register and report the actual IO5 level (S23.02).
 * true = the mux currently routes GPIO 19/20 to CAN. This is a hardware
 * readback, not the cached output shadow. */
esp_err_t tpl_ws7_read_can_available(bool* can);

// I2C mutex with bounded timeout (N13)
esp_err_t tpl_ws7_i2c_lock(uint32_t timeout_ms);
void tpl_ws7_i2c_unlock(void);

// SD card: enable open-collector push-pull mode for CS bit (S3.15)
esp_err_t tpl_ws7_sd_cs_enable_oc_pp(void);

// SD SPI bus handle (S3.16)
spi_host_device_t tpl_ws7_sd_bus(void);

// RGB panel + LVGL display (S4)
lv_display_t* tpl_ws7_display_init(uint8_t rotation, uint32_t* buf,
                                   size_t buf_size);

// GT911 touch (S5)
esp_err_t tpl_ws7_touch_init(void);
void tpl_ws7_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

/* Number of touch points in the last GT911 frame (for three-finger Home). */
int tpl_ws7_touch_points(void);

/* Last raw native GT911 point, without offsets or clamping. Returns false
 * when the last frame had no touch point. Used by the touch calibration test.
 */
bool tpl_ws7_touch_last_raw(int16_t* x, int16_t* y);

#endif  // IS_WAVESHARE_LCD7

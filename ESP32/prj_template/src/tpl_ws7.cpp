#include "tpl_ws7.h"

#ifdef IS_WAVESHARE_LCD7

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>
#include <esp_heap_caps.h>

static const char *TAG = "tpl_ws7";

static uint16_t s_output_shadow = 0;  // 12-bit shadow of CH422G output

/*
 * CH422G uses four different I2C addresses (8-bit):
 *   0x48 = WR-SET  (configuration register)
 *   0x46 = WR-OC   (open-collector settings)
 *   0x70 = WR-IO   (output data for IO0-IO7)
 *   0x4D = RD-IO   (read input data for IO0-IO7)
 *
 * For Arduino Wire (7-bit addresses), shift right by 1:
 *   0x24, 0x23, 0x38, 0x26
 */

static esp_err_t ws7_write_set(uint8_t data) {
    Wire.beginTransmission(0x24);  // 7-bit: 0x48 >> 1
    Wire.write(data);
    esp_err_t err = (Wire.endTransmission() == 0) ? ESP_OK : ESP_FAIL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write WR-SET (0x48) failed");
    }
    return err;
}

static esp_err_t ws7_write_oc(uint8_t data) {
    Wire.beginTransmission(0x23);  // 7-bit: 0x46 >> 1
    Wire.write(data);
    esp_err_t err = (Wire.endTransmission() == 0) ? ESP_OK : ESP_FAIL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write WR-OC (0x46) failed");
    }
    return err;
}

static esp_err_t ws7_write_io(uint8_t data) {
    Wire.beginTransmission(0x38);  // 7-bit: 0x70 >> 1
    Wire.write(data);
    esp_err_t err = (Wire.endTransmission() == 0) ? ESP_OK : ESP_FAIL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write WR-IO (0x70) failed");
    }
    return err;
}

static esp_err_t ws7_read_io(uint8_t *data) {
    Wire.beginTransmission(0x26);  // 7-bit: 0x4D >> 1
    if (Wire.endTransmission() != 0) {
        return ESP_FAIL;
    }
    int bytes = Wire.requestFrom(0x26, (size_t)1);
    if (bytes > 0) {
        *data = Wire.read();
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t ws7_init_ch422g(void) {
    // Write the three initialization registers (matching power-on defaults)
    esp_err_t err = ws7_write_set(0x01);  // WR-SET: IO_OE=1 (output mode)
    if (err != ESP_OK) return err;
    
    err = ws7_write_oc(0x0F);  // WR-OC: default
    if (err != ESP_OK) return err;
    
    return ws7_write_io(0xFF);  // WR-IO: all outputs high
}

static esp_err_t ws7_update_bit(uint8_t bit, bool value) {
    uint16_t reg = s_output_shadow;
    if (value) {
        reg |= (1 << bit);
    } else {
        reg &= ~(1 << bit);
    }
    s_output_shadow = reg;

    // Write to WR-IO register (bits 0-7)
    uint8_t wr_io = (uint8_t)(reg & 0xFF);
    esp_err_t err = ws7_write_io(wr_io);
    if (err != ESP_OK) {
        return err;
    }

    // Write to WR-OC register (bits 8-11) if any OC bit is set
    uint8_t wr_oc = (uint8_t)((reg >> 8) & 0xFF);
    if (wr_oc) {
        err = ws7_write_oc(wr_oc);
    }

    return err;
}

esp_err_t tpl_ws7_expander_init(void) {
    ESP_LOGI(TAG, "Initializing CH422G expander (SDA=%d, SCL=%d)",
             TPL_WS7_I2C_SDA, TPL_WS7_I2C_SCL);

    Wire.begin(TPL_WS7_I2C_SDA, TPL_WS7_I2C_SCL, 400000);

    // Verify communication: read RD-IO register
    uint8_t readback = 0;
    esp_err_t err = ws7_read_io(&readback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read from CH422G failed");
        return err;
    }

    ESP_LOGI(TAG, "CH422G ACK, readback=0x%02X", readback);

    // Initialize shadow with all outputs high (default power-on state)
    s_output_shadow = 0xFFF;

    // Initialize CH422G with power-on defaults
    err = ws7_init_ch422g();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH422G init failed");
        return err;
    }

    // Turn backlight OFF immediately (IO2 = 0)
    err = ws7_update_bit(TPL_WS7_IO_LCD_BL, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not turn off backlight immediately");
    }

    ESP_LOGI(TAG, "CH422G expander initialized successfully");
    return ESP_OK;
}

esp_err_t tpl_ws7_backlight(bool on) {
    return ws7_update_bit(TPL_WS7_IO_LCD_BL, on);
}

esp_err_t tpl_ws7_touch_reset_assert(void) {
    return ws7_update_bit(TPL_WS7_IO_TP_RST, false);  // LOW = assert reset
}

esp_err_t tpl_ws7_touch_reset_release(void) {
    esp_err_t err = ws7_update_bit(TPL_WS7_IO_TP_RST, true);  // HIGH = release
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));  // Wait for GT911 to stabilize
    }
    return err;
}

esp_err_t tpl_ws7_panel_reset(void) {
    esp_err_t err = ws7_update_bit(TPL_WS7_IO_LCD_RST, false);  // LOW = assert reset
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10));
        err = ws7_update_bit(TPL_WS7_IO_LCD_RST, true);  // HIGH = release
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(120));  // Panel initialization time
        }
    }
    return err;
}

esp_err_t tpl_ws7_sd_select(bool select) {
    // SD_CS is active-LOW: true -> drive LOW
    return ws7_update_bit(TPL_WS7_IO_SD_CS, !select);
}

esp_err_t tpl_ws7_set_usb_mode(bool usb) {
    // USB_SEL: HIGH = CAN, LOW = USB
    // usb=true -> USB mode -> IO5=LOW -> CAN disabled
    // usb=false -> CAN mode -> IO5=HIGH
    return ws7_update_bit(TPL_WS7_IO_USB_SEL, usb);
}

bool tpl_ws7_can_available(void) {
    // IO5 high = CAN available
    return (s_output_shadow & (1 << TPL_WS7_IO_USB_SEL)) != 0;
}

#endif // IS_WAVESHARE_LCD7

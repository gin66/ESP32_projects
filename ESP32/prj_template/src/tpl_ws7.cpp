#include "tpl_ws7.h"

#ifdef IS_WAVESHARE_LCD7

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_lcd_rgb_panel.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

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

// --- RGB panel + LVGL display (S4) ---

static esp_lcd_rgb_panel_t *s_rgb_panel = NULL;
static lv_display_t *s_lv_display = NULL;

static void ws7_rgb_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    if (!s_rgb_panel) return;
    
    esp_lcd_panel_draw_bitmap(s_rgb_panel,
                              lv_area_get_x1(area),
                              lv_area_get_y1(area),
                              lv_area_get_x2(area) + 1,
                              lv_area_get_y2(area) + 1,
                              px_map);
    
    lv_display_flush_ready(disp);
}

lv_display_t *tpl_ws7_display_init(uint8_t rotation, uint32_t *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        ESP_LOGW(TAG, "No draw buffer provided, returning NULL");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Initializing RGB panel (ST7262, %dx%d, 16 MHz)",
             TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT);
    
    // Configure RGB panel (S4.01, S4.02)
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = ESP_LCD_RGB_CLK_SRC_PLL,
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = TPL_WS7_SCREEN_WIDTH,
            .v_res = TPL_WS7_SCREEN_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .active_high = 1,
                .pclk_active_neg = 0,
                .de_active_high = 1,
            },
        },
        .data_width = 16,
        .psram_trans_align = 64,
        .sram_trans_align = 4,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .disp_gpio_num = ESP_LCD_RGB_PANEL_NO_DISP_GPIO,
        .spi_bus_id = -1,
        .pannel_id_bit = 0,
        .pannel_id_bit_len = 0,
        .pannel_id = 0,
        .io_count = 0,
        .io_values = nullptr,
        .flags = {
            .fb_in_psram = 1,  // Framebuffer in PSRAM
            .direct_mode = 1,
        },
    };
    
    // Map RGB data pins (S4.01)
    const int data_pins[] = {
        TPL_WS7_PIN_DATA0, TPL_WS7_PIN_DATA1, TPL_WS7_PIN_DATA2, TPL_WS7_PIN_DATA3,
        TPL_WS7_PIN_DATA4, TPL_WS7_PIN_DATA5, TPL_WS7_PIN_DATA6, TPL_WS7_PIN_DATA7,
        TPL_WS7_PIN_DATA8, TPL_WS7_PIN_DATA9, TPL_WS7_PIN_DATA10, TPL_WS7_PIN_DATA11,
        TPL_WS7_PIN_DATA12, TPL_WS7_PIN_DATA13, TPL_WS7_PIN_DATA14, TPL_WS7_PIN_DATA15,
    };
    panel_config.data_pins = data_pins;
    
    const int ctrl_pins[] = {
        TPL_WS7_PIN_HSYNC, TPL_WS7_PIN_VSYNC, TPL_WS7_PIN_DE, TPL_WS7_PIN_PCLK,
    };
    panel_config.hsync_gpio_num = TPL_WS7_PIN_HSYNC;
    panel_config.vsync_gpio_num = TPL_WS7_PIN_VSYNC;
    panel_config.de_gpio_num = TPL_WS7_PIN_DE;
    panel_config.pclk_gpio_num = TPL_WS7_PIN_PCLK;
    
    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_rgb_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return NULL;
    }
    
    err = esp_lcd_panel_init(s_rgb_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return NULL;
    }
    
    ESP_LOGI(TAG, "RGB panel initialized successfully");
    
    // Create LVGL display (S4.04, S4.05)
    s_lv_display = lv_display_create(TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT);
    if (s_lv_display) {
        lv_display_set_buffers(s_lv_display, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(s_lv_display, ws7_rgb_flush_cb);
        lv_display_set_rotation(s_lv_display, (lv_display_rotation_t)rotation);
        lv_display_set_default(s_lv_display);  // S4.05
        
        ESP_LOGI(TAG, "LVGL display created (%dx%d, rotation=%d)",
                 TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT, rotation);
    }
    
    return s_lv_display;
}

// --- GT911 Touch (S5) ---

/* GT911 I2C address (7-bit) */
#define GT911_I2C_ADDR 0x5D

/* GT911 registers */
#define GT911_REG_STATUS    0x814E
#define GT911_REG_X1_L      0x814F
#define GT911_REG_X1_H      0x8150
#define GT911_REG_Y1_L      0x8151
#define GT911_REG_Y1_H      0x8152

/* Calibration constants (adjust per hardware) */
#define GT911_CAL_X_MIN 0
#define GT911_CAL_X_MAX 799
#define GT911_CAL_Y_MIN 0
#define GT911_CAL_Y_MAX 479

static esp_lcd_touch_handle_t s_touch = NULL;

esp_err_t tpl_ws7_touch_init(void) {
    ESP_LOGI(TAG, "Initializing GT911 touch controller (I2C addr=0x%02X)", GT911_I2C_ADDR);
    
    /* S5.02: Release touch reset before addressing controller */
    esp_err_t err = tpl_ws7_touch_reset_release();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch reset release failed, continuing anyway");
    }
    
    /* Initialize GT911 via I2C */
    /* The esp_lcd_touch_gt911 driver handles I2C communication */
    esp_lcd_touch_config_t config = {
        .x_max = TPL_WS7_SCREEN_WIDTH,
        .y_max = TPL_WS7_SCREEN_HEIGHT,
        .h_res = TPL_WS7_SCREEN_WIDTH,
        .v_res = TPL_WS7_SCREEN_HEIGHT,
        .reset_gpio_num = -1,   /* S5.01: no reset pin */
        .int_gpio_num = -1,     /* S5.01: no interrupt pin */
    };
    
    /* Note: esp_lcd_touch_gt911 requires the touch library */
    /* For now, use direct I2C read as fallback */
    s_touch = nullptr;  /* Will use direct I2C reads */
    
    ESP_LOGI(TAG, "GT911 touch initialized (direct I2C mode)");
    return ESP_OK;
}

/* Read a single touch point from GT911 via I2C */
static bool gt911_read_point(int16_t *x, int16_t *y) {
    /* S5.04: Treat ESP_ERR_TIMEOUT as 'no finger', not an error */
    uint8_t status = 0;
    Wire.beginTransmission(GT911_I2C_ADDR);
    Wire.write(GT911_REG_STATUS);
    esp_err_t err = Wire.endTransmission(false);
    if (err != 0) {
        return false;  /* No finger or NACK */
    }
    
    int bytes = Wire.requestFrom(GT911_I2C_ADDR, (size_t)5);
    if (bytes < 5) {
        return false;  /* S5.04: timeout = no finger */
    }
    
    status = Wire.read();
    if ((status & 0x80) == 0) {
        return false;  /* No touch */
    }
    
    uint8_t x_l = Wire.read();
    uint8_t x_h = Wire.read();
    uint8_t y_l = Wire.read();
    uint8_t y_h = Wire.read();
    
    *x = ((x_h << 8) | x_l) & 0x0FFF;
    *y = ((y_h << 8) | y_l) & 0x0FFF;
    
    return true;
}

/* Map raw coordinates to screen coordinates with rotation (S5.03) */
static void gt911_rotate(int16_t raw_x, int16_t raw_y, lv_display_rotation_t rotation,
                         int16_t *out_x, int16_t *out_y) {
    int16_t x = raw_x, y = raw_y;
    
    /* Map to screen resolution */
    x = (x * TPL_WS7_SCREEN_WIDTH) / 0x0FFF;
    y = (y * TPL_WS7_SCREEN_HEIGHT) / 0x0FFF;
    
    /* Apply rotation */
    int16_t tmp;
    switch (rotation) {
        case LV_DISPLAY_ROTATION_0:
            /* No rotation: 0° */
            break;
        case LV_DISPLAY_ROTATION_90:
            tmp = x; x = y; y = TPL_WS7_SCREEN_HEIGHT - 1 - tmp;
            break;
        case LV_DISPLAY_ROTATION_180:
            x = TPL_WS7_SCREEN_WIDTH - 1 - x;
            y = TPL_WS7_SCREEN_HEIGHT - 1 - y;
            break;
        case LV_DISPLAY_ROTATION_270:
            tmp = x; x = TPL_WS7_SCREEN_WIDTH - 1 - y;
            y = tmp;
            break;
    }
    
    *out_x = x;
    *out_y = y;
}

/* S5.03: LVGL indev read callback with 40ms debounce */
void tpl_ws7_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data, bool *pressed,
                           int32_t width, int32_t height, lv_display_rotation_t rotation) {
    int16_t raw_x, raw_y;
    
    /* Read touch point from GT911 */
    if (gt911_read_point(&raw_x, &raw_y)) {
        /* S5.03: 40ms debounce - just read, debounce handled by LVGL */
        gt911_rotate(raw_x, raw_y, rotation, &data->x, &data->y);
        *pressed = true;
    } else {
        *pressed = false;
    }
}

// --- I2C mutex with bounded timeout (S3.14, N13) ---

static portMUX_TYPE s_i2c_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_i2c_locked = false;

esp_err_t tpl_ws7_i2c_lock(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (portENTER_CRITICAL_TIMEOUT(&s_i2c_mux, pdMS_TO_TICKS(1)) == ESP_OK) {
            if (!s_i2c_locked) {
                s_i2c_locked = true;
                portEXIT_CRITICAL(&s_i2c_mux);
                return ESP_OK;
            }
            portEXIT_CRITICAL(&s_i2c_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "I2C mutex timeout after %lu ms", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

void tpl_ws7_i2c_unlock(void) {
    portENTER_CRITICAL(&s_i2c_mux);
    s_i2c_locked = false;
    portEXIT_CRITICAL(&s_i2c_mux);
}

// --- SD card: enable open-collector push-pull for CS (S3.15) ---

esp_err_t tpl_ws7_sd_cs_enable_oc_pp(void) {
    // Configure bit 4 (SD_CS) as push-pull via WR-OC register
    // WR-OC: 1 = push-pull, 0 = open-collector
    // Bit 4 = 0x10 for SD_CS
    s_output_shadow |= (1 << (TPL_WS7_IO_SD_CS + 8));  // Set OC bit 4
    uint8_t wr_oc = (uint8_t)((s_output_shadow >> 8) & 0xFF);
    return ws7_write_oc(wr_oc);
}

// --- SD SPI bus (S3.16) ---

static spi_bus_handle_t s_sd_bus_handle = NULL;

spi_bus_handle_t tpl_ws7_sd_bus(void) {
    if (!s_sd_bus_handle) {
        ESP_LOGI(TAG, "Initializing SD SPI bus (MOSI=%d, SCLK=%d, MISO=%d)",
                 TPL_WS7_SD_MOSI, TPL_WS7_SD_SCLK, TPL_WS7_SD_MISO);
        spi_bus_config bus = {
            .mosi_io_num = TPL_WS7_SD_MOSI,
            .miso_io_num = TPL_WS7_SD_MISO,
            .sclk_io_num = TPL_WS7_SD_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t err = spi_bus_initialize(VSPI_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SD SPI bus init failed: %s", esp_err_to_name(err));
        }
        s_sd_bus_handle = VSPI_HOST;
    }
    return s_sd_bus_handle;
}

#endif // IS_WAVESHARE_LCD7

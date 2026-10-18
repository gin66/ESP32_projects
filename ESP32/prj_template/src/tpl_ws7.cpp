#include "tpl_ws7.h"

#include "tpl_debug_log.h"

#ifdef IS_WAVESHARE_LCD7

#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_types.h>
#include <esp_log.h>
#include <lvgl.h>

static const char* TAG = "tpl_ws7";

/* RGB timing defaults. A project can override any of these in platformio.ini,
 * e.g. -DTPL_WS7_HSYNC_BACK_PORCH=20, but these are the proven Waveshare 7"
 * values; wider porches (10/20/16) made the OTA stripes worse on this panel. */
#ifndef TPL_WS7_HSYNC_PULSE_WIDTH
#define TPL_WS7_HSYNC_PULSE_WIDTH 4
#endif
#ifndef TPL_WS7_HSYNC_BACK_PORCH
#define TPL_WS7_HSYNC_BACK_PORCH 8
#endif
#ifndef TPL_WS7_HSYNC_FRONT_PORCH
#define TPL_WS7_HSYNC_FRONT_PORCH 8
#endif
#ifndef TPL_WS7_VSYNC_PULSE_WIDTH
#define TPL_WS7_VSYNC_PULSE_WIDTH 4
#endif
#ifndef TPL_WS7_VSYNC_BACK_PORCH
#define TPL_WS7_VSYNC_BACK_PORCH 8
#endif
#ifndef TPL_WS7_VSYNC_FRONT_PORCH
#define TPL_WS7_VSYNC_FRONT_PORCH 8
#endif
#ifndef TPL_WS7_PCLK_IDLE_HIGH
#define TPL_WS7_PCLK_IDLE_HIGH 1
#endif
#ifndef TPL_WS7_PCLK_HZ
#define TPL_WS7_PCLK_HZ (16 * 1000 * 1000)
#endif

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

static esp_err_t ws7_read_io(uint8_t* data) {
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
  /* Rev 1.2 (ST7262): idle high, then >= 10 ms low and >= 120 ms after
   * release. Use the conservative 20/200 ms figures: they cost nothing on
   * boot and cover a panel that was left in an odd state by a previous run
   * (the reset line is shared through the CH422G, so an MCU-only reset does
   * not otherwise recover it). */
  esp_err_t err = ws7_update_bit(TPL_WS7_IO_LCD_RST, true);
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(10));
    err = ws7_update_bit(TPL_WS7_IO_LCD_RST, false);  // LOW = assert reset
  }
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(20));
    err = ws7_update_bit(TPL_WS7_IO_LCD_RST, true);  // HIGH = release
    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(200));  // panel initialization time
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
  // usb=true  -> IO5 LOW  -> USB mode
  // usb=false -> IO5 HIGH -> CAN mode
  return ws7_update_bit(TPL_WS7_IO_USB_SEL, !usb);
}

bool tpl_ws7_can_available(void) {
  // IO5 high = CAN available
  return (s_output_shadow & (1 << TPL_WS7_IO_USB_SEL)) != 0;
}

esp_err_t tpl_ws7_read_can_available(bool* can) {
  if (!can) return ESP_ERR_INVALID_ARG;

  if (tpl_ws7_i2c_lock(50) != ESP_OK) return ESP_ERR_TIMEOUT;
  uint8_t io = 0;
  const esp_err_t err = ws7_read_io(&io);
  tpl_ws7_i2c_unlock();
  if (err != ESP_OK) return err;

  *can = (io & (1 << TPL_WS7_IO_USB_SEL)) != 0;
  return ESP_OK;
}

// --- RGB panel + LVGL display (S4) ---

static esp_lcd_panel_handle_t s_rgb_panel = NULL;
static lv_display_t* s_lv_display = NULL;

/* Scratch buffer for rotating a flushed band from the logical (rotated)
 * coordinate space into the panel's native orientation. LVGL hands a custom
 * flush callback un-rotated pixels and expects the driver to rotate. */
static uint8_t* s_rot_buf = NULL;

static void ws7_rgb_flush_cb(lv_display_t* disp, const lv_area_t* area,
                             uint8_t* px_map) {
  if (!s_rgb_panel) {
    lv_display_flush_ready(disp);
    return;
  }

  lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (rot == LV_DISPLAY_ROTATION_0 || s_rot_buf == NULL) {
    esp_lcd_panel_draw_bitmap(s_rgb_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
    return;
  }

  /* Rotate the area and the pixel data into native panel coordinates. */
  lv_area_t rot_area = *area;
  lv_display_rotate_area(disp, &rot_area);

  int32_t src_w = lv_area_get_width(area);
  int32_t src_h = lv_area_get_height(area);
  int32_t dst_w = lv_area_get_width(&rot_area);

  lv_draw_sw_rotate(px_map, s_rot_buf, src_w, src_h, src_w * 2, dst_w * 2, rot,
                    LV_COLOR_FORMAT_RGB565);

  esp_lcd_panel_draw_bitmap(s_rgb_panel, rot_area.x1, rot_area.y1,
                            rot_area.x2 + 1, rot_area.y2 + 1, s_rot_buf);

  lv_display_flush_ready(disp);
}

/* Create (or re-create) the RGB panel from the compile-time TPL_WS7_* timing
 * defaults. Kept separate from the LVGL display so Settings can reset the panel
 * without destroying the UI. */
static esp_err_t ws7_rgb_panel_create(void) {
  // Configure RGB panel (S4.01, S4.02)
  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src = LCD_CLK_SRC_PLL160M;
  panel_config.timings.pclk_hz = TPL_WS7_PCLK_HZ;
  panel_config.timings.h_res = TPL_WS7_SCREEN_WIDTH;
  panel_config.timings.v_res = TPL_WS7_SCREEN_HEIGHT;
  panel_config.timings.hsync_pulse_width = TPL_WS7_HSYNC_PULSE_WIDTH;
  panel_config.timings.hsync_back_porch = TPL_WS7_HSYNC_BACK_PORCH;
  panel_config.timings.hsync_front_porch = TPL_WS7_HSYNC_FRONT_PORCH;
  panel_config.timings.vsync_pulse_width = TPL_WS7_VSYNC_PULSE_WIDTH;
  panel_config.timings.vsync_back_porch = TPL_WS7_VSYNC_BACK_PORCH;
  panel_config.timings.vsync_front_porch = TPL_WS7_VSYNC_FRONT_PORCH;
  panel_config.timings.flags.hsync_idle_low = 0;  // active high
  panel_config.timings.flags.vsync_idle_low = 0;  // active high
  panel_config.timings.flags.de_idle_high = 0;
  panel_config.timings.flags.pclk_active_neg = 0;
  panel_config.timings.flags.pclk_idle_high = TPL_WS7_PCLK_IDLE_HIGH;
  panel_config.data_width = 16;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
  panel_config.sram_trans_align = 4;
  panel_config.psram_trans_align = 64;
#endif

  // Control pins (S4.01)
  panel_config.hsync_gpio_num = TPL_WS7_PIN_HSYNC;
  panel_config.vsync_gpio_num = TPL_WS7_PIN_VSYNC;
  panel_config.de_gpio_num = TPL_WS7_PIN_DE;
  panel_config.pclk_gpio_num = TPL_WS7_PIN_PCLK;

  // Map RGB data pins (S4.01)
  const int data_pins[16] = {
      TPL_WS7_PIN_DATA0,  TPL_WS7_PIN_DATA1,  TPL_WS7_PIN_DATA2,
      TPL_WS7_PIN_DATA3,  TPL_WS7_PIN_DATA4,  TPL_WS7_PIN_DATA5,
      TPL_WS7_PIN_DATA6,  TPL_WS7_PIN_DATA7,  TPL_WS7_PIN_DATA8,
      TPL_WS7_PIN_DATA9,  TPL_WS7_PIN_DATA10, TPL_WS7_PIN_DATA11,
      TPL_WS7_PIN_DATA12, TPL_WS7_PIN_DATA13, TPL_WS7_PIN_DATA14,
      TPL_WS7_PIN_DATA15,
  };
  for (int i = 0; i < 16; i++) {
    panel_config.data_gpio_nums[i] = data_pins[i];
  }

  panel_config.disp_gpio_num = -1;  // no display-enable GPIO
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
  panel_config.on_frame_trans_done = NULL;
  panel_config.user_ctx = NULL;
#endif
  panel_config.flags.fb_in_psram =
      1;  // framebuffer in PSRAM (750 KB won't fit SRAM)
  /* Bounce buffers: the RGB DMA reads from internal RAM instead of streaming
   * the PSRAM framebuffer, which is the recommended IDF setup. It also skips
   * the full-frame cache writeback that esp_lcd does when drawing straight
   * into a PSRAM frame buffer. esp_lcd allocates TWO of these, so each line
   * costs 2 × width × 2 B of internal DMA RAM. */
#ifndef TPL_WS7_BOUNCE_LINES
#define TPL_WS7_BOUNCE_LINES 10
#endif
  panel_config.bounce_buffer_size_px =
      TPL_WS7_SCREEN_WIDTH * TPL_WS7_BOUNCE_LINES;

  esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &s_rgb_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RGB panel create failed: %s", esp_err_to_name(err));
    s_rgb_panel = NULL;
    return err;
  }

  err = esp_lcd_panel_init(s_rgb_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
    esp_lcd_panel_del(s_rgb_panel);
    s_rgb_panel = NULL;
    return err;
  }

  ESP_LOGI(TAG, "RGB panel initialized successfully");
  return ESP_OK;

}

lv_display_t* tpl_ws7_display_init(uint8_t rotation, uint32_t* buf,
                                   size_t buf_size) {
  if (!buf || buf_size == 0) {
    ESP_LOGW(TAG, "No draw buffer provided, returning NULL");
    return NULL;
  }

  /* Translate the project rotation index (whitepaper §6.6) into the LVGL
   * enum. Index 0 is portrait and must NOT be passed through as rotation 0,
   * which would leave the panel in landscape. */
  lv_display_rotation_t lv_rotation;
  switch (rotation) {
    case 0:
      lv_rotation = LV_DISPLAY_ROTATION_90;
      break;  // portrait
    case 1:
      lv_rotation = LV_DISPLAY_ROTATION_0;
      break;  // landscape
    case 2:
      lv_rotation = LV_DISPLAY_ROTATION_270;
      break;  // portrait, 180°
    case 3:
      lv_rotation = LV_DISPLAY_ROTATION_180;
      break;  // landscape, mirrored
    default:
      lv_rotation = LV_DISPLAY_ROTATION_90;
      break;
  }

  ESP_LOGI(TAG, "Initializing RGB panel (ST7262, %dx%d, %u Hz)",
           TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT,
           (unsigned)TPL_WS7_PCLK_HZ);

  if (ws7_rgb_panel_create() != ESP_OK) return NULL;

  // Scratch buffer for software rotation. The CPU rotates into it and
  // draw_bitmap copies the result into the PSRAM framebuffer, so the scratch
  // buffer itself is not a DMA source. DIRAM has only a few KB free after the
  // static image, which is why this lives in PSRAM.
  if (lv_rotation != LV_DISPLAY_ROTATION_0 && s_rot_buf == NULL) {
    s_rot_buf = (uint8_t*)heap_caps_malloc(buf_size,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rot_buf == NULL) {
      ESP_LOGW(TAG, "Rotation buffer alloc failed; rotation will be skipped");
    }
  }

  // Create LVGL display (S4.04, S4.05)
  s_lv_display = lv_display_create(TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT);
  if (s_lv_display) {
    lv_display_set_buffers(s_lv_display, buf, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_lv_display, ws7_rgb_flush_cb);
    lv_display_set_rotation(s_lv_display, lv_rotation);
    lv_display_set_default(s_lv_display);  // S4.05

    ESP_LOGI(TAG,
             "LVGL display created (%dx%d native, rotation idx %d -> lvgl %d)",
             TPL_WS7_SCREEN_WIDTH, TPL_WS7_SCREEN_HEIGHT, rotation,
             (int)lv_rotation);
  }

  return s_lv_display;
}

// --- Runtime RGB panel reset (S4.06) ---

esp_err_t tpl_ws7_reset_rgb_timing(void) {
  if (!s_rgb_panel) {
    ESP_LOGE(TAG, "RGB panel not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  /* IDF 5.5 has no esp_lcd_rgb_panel_set_timings()/get_config(). Recreating the
   * panel is the only public way to put every timing register back to the
   * compile-time defaults, and it also restarts the DMA. The LVGL display and
   * its draw buffers stay alive; the flush callback picks up the new handle. */
  esp_err_t err = esp_lcd_panel_del(s_rgb_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RGB panel delete failed: %s", esp_err_to_name(err));
    return err;
  }
  s_rgb_panel = NULL;

  /* Reset the ST7262 through CH422G IO3 as well. The timing registers live in
   * the ESP32 RGB peripheral, so the recreate below is what restores them; the
   * panel reset clears the controller's own state. */
  err = tpl_ws7_panel_reset();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ST7262 reset via CH422G failed: %s", esp_err_to_name(err));
  }

  err = ws7_rgb_panel_create();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RGB panel recreate failed: %s", esp_err_to_name(err));
    return err;
  }

  if (s_lv_display) {
    lv_obj_invalidate(lv_screen_active());
  }
  ESP_LOGI(TAG, "RGB panel reset to compile-time timing");
  return ESP_OK;
}

// --- GT911 Touch (S5) ---

/* GT911 I2C addresses (7-bit). The address is latched from the INT pin level at
 * reset release; the Waveshare reference uses 0x5D, 0x14 is the other option.
 */
#define GT911_I2C_ADDR_PRIMARY 0x5D
#define GT911_I2C_ADDR_ALT 0x14

/* GT911 registers */
#define GT911_REG_STATUS 0x814E
#define GT911_REG_X1_L 0x814F
#define GT911_REG_X1_H 0x8150
#define GT911_REG_Y1_L 0x8151
#define GT911_REG_Y1_H 0x8152

/* Calibration constants (adjust per hardware) */
#define GT911_CAL_X_MIN 0
#define GT911_CAL_X_MAX 799
#define GT911_CAL_Y_MIN 0
#define GT911_CAL_Y_MAX 479

/* Detected GT911 address (0 = not present). */
static volatile uint8_t s_touch_points = 0;
static uint8_t s_touch_addr = 0;

/* Last raw point for touch diagnostics/calibration. */
static volatile int16_t s_last_raw_x = 0;
static volatile int16_t s_last_raw_y = 0;
static volatile bool s_last_raw_valid = false;

/* Probe for an ACKing device. beginTransmission/endTransmission do not log on
 * NACK (unlike requestFrom), so a bus scan stays quiet. */
static bool ws7_i2c_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

esp_err_t tpl_ws7_touch_init(void) {
  ESP_LOGI(TAG, "Initializing GT911 touch controller");

  /* S5.02: Sequence the CH422G reset line before addressing the controller.
   * Reset is CH422G IO1; the GT911 NACKs while held in reset. */
  tpl_ws7_touch_reset_assert();
  vTaskDelay(pdMS_TO_TICKS(10));
  esp_err_t err = tpl_ws7_touch_reset_release();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Touch reset release failed, continuing anyway");
  }
  vTaskDelay(pdMS_TO_TICKS(50)); /* GT911 firmware boot time */

  const uint8_t candidates[] = {GT911_I2C_ADDR_PRIMARY, GT911_I2C_ADDR_ALT};
  for (size_t i = 0; i < sizeof(candidates); i++) {
    if (ws7_i2c_probe(candidates[i])) {
      s_touch_addr = candidates[i];
      break;
    }
  }

  if (s_touch_addr == 0) {
    ESP_LOGE(TAG, "GT911 not found on I2C (tried 0x%02X, 0x%02X)",
             GT911_I2C_ADDR_PRIMARY, GT911_I2C_ADDR_ALT);
    TPL_LOGI("ws7", "[tpl_ws7] GT911 NOT FOUND (tried 0x%02X, 0x%02X)\n",
             GT911_I2C_ADDR_PRIMARY, GT911_I2C_ADDR_ALT);
    return ESP_FAIL;
  }

  TPL_LOGI("ws7", "[tpl_ws7] GT911 at 0x%02X\n", s_touch_addr);
  return ESP_OK;
}

/* Read a single touch point from GT911 via I2C (S5.01) */
static bool gt911_read_point(int16_t* x, int16_t* y) {
  s_touch_points = 0;
  s_last_raw_valid = false;
  if (s_touch_addr == 0) {
    return false;
  }
  if (tpl_ws7_i2c_lock(20) != ESP_OK) {
    return false; /* bus busy: report no touch rather than block the UI */
  }

  Wire.beginTransmission(s_touch_addr);
  Wire.write((uint8_t)(GT911_REG_STATUS >> 8));
  Wire.write((uint8_t)(GT911_REG_STATUS & 0xFF));
  /* S5.04: treat a timeout/NACK as 'no finger', not an error */
  if (Wire.endTransmission(false) != 0) {
    tpl_ws7_i2c_unlock();
    return false;
  }

  if (Wire.requestFrom((int)s_touch_addr, (size_t)6) < 6) {
    tpl_ws7_i2c_unlock();
    return false;
  }

  uint8_t status = Wire.read();
  if ((status & 0x80) != 0) s_touch_points = (uint8_t)(status & 0x0F);

  /* Register map from 0x814E:
   *   0x814E status, 0x814F track id, 0x8150 Xlow, 0x8151 Xhigh,
   *   0x8152 Ylow, 0x8153 Yhigh. */
  uint8_t track_id = Wire.read();
  uint8_t x_l = Wire.read();
  uint8_t x_h = Wire.read();
  uint8_t y_l = Wire.read();
  uint8_t y_h = Wire.read();
  (void)track_id;

  /* Bit 7 = buffer ready, low nibble = number of touch points. */
  if ((status & 0x80) == 0) {
    tpl_ws7_i2c_unlock();
    return false; /* no new data */
  }

  /* Acknowledge the buffer-ready flag. This must happen on EVERY ready frame,
   * even one with zero points, otherwise the GT911 stops raising new events. */
  Wire.beginTransmission(s_touch_addr);
  Wire.write((uint8_t)(GT911_REG_STATUS >> 8));
  Wire.write((uint8_t)(GT911_REG_STATUS & 0xFF));
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();

  tpl_ws7_i2c_unlock();

  if ((status & 0x0F) == 0) {
    return false; /* ready frame but no touch points */
  }

  *x = (int16_t)(((x_h << 8) | x_l) & 0x0FFF);
  *y = (int16_t)(((y_h << 8) | y_l) & 0x0FFF);
  s_last_raw_x = *x;
  s_last_raw_y = *y;
  s_last_raw_valid = true;
  return true;
}

bool tpl_ws7_touch_last_raw(int16_t* x, int16_t* y) {
  if (!s_last_raw_valid) return false;
  if (x) *x = s_last_raw_x;
  if (y) *y = s_last_raw_y;
  return true;
}

/* S5.03: LVGL indev read callback. LVGL expects the point in the panel's
 * native coordinate space and applies the display rotation itself. */
int tpl_ws7_touch_points(void) { return (int)s_touch_points; }

void tpl_ws7_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  int16_t raw_x = 0, raw_y = 0;

  if (gt911_read_point(&raw_x, &raw_y)) {
    /* The GT911 is configured for the panel resolution (800x480), so its
     * raw coordinates are already native panel pixels. A small constant
     * offset corrects the panel/bezel mapping; with rotation index 0 LVGL
     * maps logical y to native x, so the y correction is an x offset. */
#ifndef TPL_WS7_TOUCH_X_OFFSET
#define TPL_WS7_TOUCH_X_OFFSET 0
#endif
#ifndef TPL_WS7_TOUCH_Y_OFFSET
#define TPL_WS7_TOUCH_Y_OFFSET 0
#endif
    int32_t nx = raw_x + TPL_WS7_TOUCH_X_OFFSET;
    int32_t ny = raw_y + TPL_WS7_TOUCH_Y_OFFSET;
    if (nx < 0) nx = 0;
    if (nx > TPL_WS7_SCREEN_WIDTH - 1) nx = TPL_WS7_SCREEN_WIDTH - 1;
    if (ny < 0) ny = 0;
    if (ny > TPL_WS7_SCREEN_HEIGHT - 1) ny = TPL_WS7_SCREEN_HEIGHT - 1;

    data->point.x = nx;
    data->point.y = ny;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// --- I2C mutex with bounded timeout (S3.14, N13) ---

static SemaphoreHandle_t s_i2c_mux = NULL;

esp_err_t tpl_ws7_i2c_lock(uint32_t timeout_ms) {
  if (s_i2c_mux == NULL) {
    s_i2c_mux = xSemaphoreCreateMutex();
    if (s_i2c_mux == NULL) {
      ESP_LOGE(TAG, "Failed to create I2C mutex");
      return ESP_FAIL;
    }
  }
  if (xSemaphoreTake(s_i2c_mux, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    ESP_LOGW(TAG, "I2C mutex timeout after %lu ms", (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

void tpl_ws7_i2c_unlock(void) {
  if (s_i2c_mux != NULL) {
    xSemaphoreGive(s_i2c_mux);
  }
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

static bool s_sd_bus_initialized = false;
static spi_host_device_t s_sd_bus_handle = SPI3_HOST;

spi_host_device_t tpl_ws7_sd_bus(void) {
  if (!s_sd_bus_initialized) {
    ESP_LOGI(TAG, "Initializing SD SPI bus (MOSI=%d, SCLK=%d, MISO=%d)",
             TPL_WS7_SD_MOSI, TPL_WS7_SD_SCLK, TPL_WS7_SD_MISO);
    spi_bus_config_t bus = {
        .mosi_io_num = TPL_WS7_SD_MOSI,
        .miso_io_num = TPL_WS7_SD_MISO,
        .sclk_io_num = TPL_WS7_SD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(s_sd_bus_handle, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "SD SPI bus init failed: %s", esp_err_to_name(err));
    }
    s_sd_bus_initialized = true;
  }
  return s_sd_bus_handle;
}

#endif  // IS_WAVESHARE_LCD7

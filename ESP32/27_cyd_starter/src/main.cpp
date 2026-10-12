/**
 * 27_cyd_starter — Minimal CYD board abstraction demo
 *
 * Demonstrates the tpl_display abstraction for 2.8″ and 3.5″ CYD boards.
 * This is a standalone demo — no full template stack required.
 *
 * To use:
 *   1. Select your board define in platformio.ini:
 *      - 2.8″:  -D IS_ESP32_CYD_28
 *      - 3.5″:  -D IS_ESP32_CYD_35
 *   2. Build and upload:
 *      pio run -e esp32ota    (OTA, default)
 *      pio run -e esp32usb    (USB serial)
 *
 * The abstraction handles:
 *   - Board detection (dimensions, driver, all pins, calibration)
 *   - Display initialization (TFT_eSPI + LVGL driver)
 *   - Touch initialization (private or shared SPI bus)
 *   - Full rotation pipeline (ADC → calibration → logical rotation)
 *   - Backlight PWM, RGB LED, CDS light sensor (3.5″ only)
 *
 * See: prj_template/docs/cyd_board_abstraction.md
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Include the CYD board abstraction header
#include "tpl_display.h"

// LVGL draw buffer (resolution-dependent, from CYD dimensions)
uint32_t draw_buf[CYD_DRAW_BUF_SIZE / 4];

// Touchscreen — SPI bus initialized by tpl_cyd_touch_init()
XPT2046_Touchscreen touchscreen(CYD_TOUCH_CS, CYD_TOUCH_IRQ);

// Global TFT_eSPI instance (required by TFT_eSPI library and tpl_display)
TFT_eSPI tft = TFT_eSPI();

// LVGL display handle (returned by tpl_cyd_display_init)
static lv_display_t* g_disp = NULL;

// Frame counter
static uint32_t g_frame_count = 0;
static lv_obj_t* g_lblInfo = NULL;
static lv_obj_t* g_lblFrame = NULL;

// Touch callback — the abstraction handles the full rotation pipeline
static void touchscreen_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  bool pressed;
  tpl_cyd_touch_read(touchscreen, data, &pressed, CYD_WIDTH, CYD_HEIGHT,
                     TFT_ROTATION);
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void setup() {
  Serial.begin(115200);
  Serial.println("27_cyd_starter — CYD Board Abstraction Demo");

  // Initialize LVGL
  lv_init();

  // Initialize CYD display and touch (abstraction handles everything)
  g_disp = tpl_cyd_display_init(TFT_ROTATION, draw_buf, sizeof(draw_buf));
  lv_display_set_default(g_disp);  // Required: lv_indev_create() needs it
  tpl_cyd_touch_init(touchscreen, TFT_ROTATION);

  // Create and register input device (touch)
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read_cb);

  // Initialize CYD backlight PWM (channel 7, dim brightness)
  tpl_cyd_backlight_init(7, 51);

  // Initialize CYD RGB LED pins (HIGH = off on both boards)
  tpl_cyd_rgb_init(true);

  // Create a simple UI: centered text showing board info
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000),
                            LV_PART_MAIN);

  char info[80];
#if defined(IS_ESP32_CYD_28)
  snprintf(info, sizeof(info), "2.8\" CYD\n%dx%d\nILI9341", CYD_SCREEN_WIDTH,
           CYD_SCREEN_HEIGHT);
#elif defined(IS_ESP32_CYD_35)
  snprintf(info, sizeof(info), "3.5\" CYD\n%dx%d\nST7796", CYD_SCREEN_WIDTH,
           CYD_SCREEN_HEIGHT);
#else
  snprintf(info, sizeof(info), "Unknown CYD board");
#endif

  g_lblInfo = lv_label_create(lv_screen_active());
  lv_label_set_text(g_lblInfo, info);
  lv_obj_set_style_text_color(g_lblInfo, lv_color_hex(0x00FF00), LV_PART_MAIN);

  lv_obj_align(g_lblInfo, LV_ALIGN_CENTER, 0, -20);

  g_lblFrame = lv_label_create(lv_screen_active());
  lv_label_set_text(g_lblFrame, "Frames: 0");

  lv_obj_align(g_lblFrame, LV_ALIGN_CENTER, 0, 20);

  Serial.println("[CYD] Setup complete — showing board info");
}

void loop() {
  static unsigned long last_update = 0;
  unsigned long now = millis();

  // Update frame counter every second
  if (now - last_update > 1000) {
    last_update = now;
    g_frame_count++;
    char buf[32];
    snprintf(buf, sizeof(buf), "Frames: %lu", g_frame_count);
    lv_label_set_text(g_lblFrame, buf);
  }

  // LVGL tasks
  lv_task_handler();
  vTaskDelay(5 / portTICK_PERIOD_MS);
}

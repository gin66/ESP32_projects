/**
 * @file tpl_display.cpp
 * @brief CYD board abstraction layer implementation
 *
 * Implements display and touch initialization functions for 2.8″ and 3.5″
 * CYD boards. See tpl_display.h for API documentation.
 */

#include "tpl_display.h"

// Internal state: tracks whether display has been initialized (needed for
// 3.5" board SPI sharing enforcement).
static bool g_cyd_display_initialized = false;

// ============================================================================
// tpl_cyd_display_init() — Phase 1.3
// ============================================================================

lv_display_t * tpl_cyd_display_init(uint8_t rotation) {
  // Initialize TFT_eSPI with board-specific settings
  extern TFT_eSPI tft;  // Declared in project code
  tft.begin();

  // Apply color inversion per board (ILI9341 needs it, ST7796 does not)
#ifdef TFT_INVERSION_ON
  tft.invertDisplay(true);
#else
  tft.invertDisplay(false);
#endif

  // Set the display rotation
  tft.setRotation(rotation);

  // Create LVGL display driver with native (portrait) resolution
  // The driver handles rotation internally via lv_display_set_rotation()
  extern TFT_eSPI tft;  // The global TFT_eSPI instance
  lv_display_t * disp = lv_tft_espi_create(CYD_SCREEN_WIDTH, CYD_SCREEN_HEIGHT,
                                           nullptr, 0);

  // Map C++ rotation (0–3) to LVGL rotation
  switch (rotation) {
    case 0:  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);  break;
    case 1:  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);  break;
    case 2:  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180); break;
    case 3:  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270); break;
    default: lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);  break;
  }

  g_cyd_display_initialized = true;
  return disp;
}

// ============================================================================
// tpl_cyd_touch_init() — Phase 1.4
// ============================================================================

void tpl_cyd_touch_init(XPT2046_Touchscreen &touchscreen, uint8_t rotation) {
#if CYD_TOUCH_HAS_PRIVATE_SPI
  // 2.8" board: private SPI bus — begin with dedicated pins from
  // tpl_display.h. The abstraction is self-contained.
  SPIClass tsSpi = SPIClass(VSPI);
  tsSpi.begin(CYD_TOUCH_CLK, CYD_TOUCH_MISO, CYD_TOUCH_MOSI);
  touchscreen.begin(tsSpi);
  Serial.println("[CYD] 2.8\" CYD — XPT2046 touch on private SPI bus");
#else
  // 3.5" board: shares display SPI — must be called AFTER display init
  if (!g_cyd_display_initialized) {
    Serial.println("[CYD] ERROR: Call tpl_cyd_display_init() before "
                   "tpl_cyd_touch_init()");
    Serial.println("[CYD] The 3.5\" board shares the display SPI bus.");
    return;
  }
  // Reuse TFT_eSPI's SPI instance (shared bus)
  touchscreen.begin(TFT_eSPI::getSPIinstance());
  Serial.println("[CYD] 3.5\" CYD — XPT2046 touch on shared display SPI");
#endif

  // Set touch rotation (XPT2046 library handles ADC coordinate transform)
  touchscreen.setRotation(rotation);
}

// ============================================================================
// tpl_cyd_touch_read() — Phase 1.5 (CORE FUNCTION)
// ============================================================================

void tpl_cyd_touch_read(XPT2046_Touchscreen &touchscreen,
                        lv_indev_data_t *data, bool *pressed,
                        int32_t screen_w, int32_t screen_h,
                        uint8_t rotation) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get raw ADC point from XPT2046 library
    // The library's update() already applies its internal rotation transform
    TS_Point p = touchscreen.getPoint();

    // Map rotated ADC coordinates to portrait screen coordinates using
    // board-specific calibration ranges (from tpl_display.h)
    int32_t lx = constrain(map(p.x, CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX,
                               1, screen_w),
                           0, screen_w - 1);
    int32_t ly = constrain(map(p.y, CYD_TOUCH_Y_MIN, CYD_TOUCH_Y_MAX,
                               1, screen_h),
                           0, screen_h - 1);

    // Apply the requested logical rotation (portrait → landscape or vice versa)
    // This maps portrait coordinates to the requested display orientation
    int32_t final_x, final_y;
    switch (rotation) {
      case 0:  // Portrait — no swap
        final_x = lx;
        final_y = ly;
        break;
      case 1:  // 90° clockwise: (x, y) → (y, W-1-x)
        final_x = ly;
        final_y = screen_w - 1 - lx;
        break;
      case 2:  // 180°: (x, y) → (W-1-x, H-1-y)
        final_x = screen_w - 1 - lx;
        final_y = screen_h - 1 - ly;
        break;
      case 3:  // 270° clockwise: (x, y) → (H-1-y, x)
        final_x = screen_h - 1 - ly;
        final_y = lx;
        break;
      default:
        final_x = lx;
        final_y = ly;
        break;
    }

    data->point.x = final_x;
    data->point.y = final_y;
    *pressed = true;
  } else {
    *pressed = false;
  }
}

// ============================================================================
// tpl_cyd_backlight_init() — Phase 1.6
// ============================================================================

void tpl_cyd_backlight_init(int pwm_channel, uint8_t initial_brightness) {
  ledcSetup(pwm_channel, 5000, 8);  // 8-bit PWM at 5kHz
  ledcAttachPin(CYD_BACKLIGHT_PIN, pwm_channel);
  ledcWrite(pwm_channel, initial_brightness);
}

// ============================================================================
// tpl_cyd_rgb_init() — Phase 1.7
// ============================================================================

void tpl_cyd_rgb_init(bool turn_off_rgb) {
  pinMode(CYD_RGB_RED, OUTPUT);
  pinMode(CYD_RGB_GREEN, OUTPUT);
  pinMode(CYD_RGB_BLUE, OUTPUT);

  if (turn_off_rgb) {
    // 2.8" board: inverted logic — HIGH = LEDs off
    // 3.5" board: floating pins — HIGH = prevent floating state
    // Same state (HIGH), different hardware reasons — works for both.
    digitalWrite(CYD_RGB_RED, HIGH);
    digitalWrite(CYD_RGB_GREEN, HIGH);
    digitalWrite(CYD_RGB_BLUE, HIGH);
  } else {
    // Turn LEDs on (2.8" uses inverted logic, so LOW = on)
    digitalWrite(CYD_RGB_RED, LOW);
    digitalWrite(CYD_RGB_GREEN, LOW);
    digitalWrite(CYD_RGB_BLUE, LOW);
  }
}

// ============================================================================
// tpl_cyd_light_sensor_read() — Phase 1.8
// ============================================================================

uint16_t tpl_cyd_light_sensor_read(void) {
#if CYD_HAS_CDS
  return analogRead(CYD_CDS_PIN);
#else
  (void)0;  // No CDS on 2.8" boards
  return 0;
#endif
}

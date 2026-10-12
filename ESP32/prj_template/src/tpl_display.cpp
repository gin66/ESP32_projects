/**
 * @file tpl_display.cpp
 * @brief CYD board abstraction layer implementation
 *
 * Implements display and touch initialization functions for 2.8″ and 3.5″
 * CYD boards. See tpl_display.h for API documentation.
 */

#include "tpl_display.h"

/* All implementation below uses CYD-specific types (TFT_eSPI,
 * XPT2046_Touchscreen, lvgl). Guard so non-CYD projects that include
 * template.h can still build without these libraries. */
#if defined(IS_ESP32_CYD_28) || defined(IS_ESP32_CYD_35)

// Internal state: tracks whether display has been initialized (needed for
// 3.5" board SPI sharing enforcement).
static bool g_cyd_display_initialized = false;

// ============================================================================
// Colour inversion state
// ============================================================================

/* The 2.8" ILI9341 comes up colour-inverted (black background renders white)
 * and INVON is the only thing that corrects it — MADCTL bit 3 is not, which
 * was verified by reading MADCTL back. The 3.5" ST7796 is correct without it,
 * so the default comes from the board (CYD_NEEDS_INVERSION). Kept as state so
 * projects can flip it at runtime (a long press on the header band in
 * 26_CYD_CAN_monitor) instead of paying for a reflash per attempt. */
static bool g_cyd_inversion = CYD_NEEDS_INVERSION;
static bool g_cyd_inversion_pending = false;

void tpl_cyd_set_inversion(bool on) {
  g_cyd_inversion = on;
  g_cyd_inversion_pending = true;
}

bool tpl_cyd_get_inversion(void) { return g_cyd_inversion; }

void tpl_cyd_apply_inversion(void) {
  if (!g_cyd_inversion_pending) return;
  g_cyd_inversion_pending = false;
  extern TFT_eSPI tft;  // Declared in project code
  /* Must run while no other TFT_eSPI instance holds a transaction: the LVGL
   * driver instance rewrites MADCTL/INV during its own flush, and a command
   * sent from inside that window is silently lost. Call this from loop(),
   * outside lv_task_handler(). */
  tft.invertDisplay(g_cyd_inversion);
}

// ============================================================================
// tpl_cyd_display_init() — Phase 1.3
// ============================================================================

lv_display_t* tpl_cyd_display_init(uint8_t rotation, void* buffer,
                                   uint32_t buffer_size) {
  Serial.println("[CYD] === tpl_cyd_display_init START ===");
  Serial.printf("[CYD] rotation=%d, buffer=%p, buf_size=%u\n", rotation, buffer,
                buffer_size);
  delay(50);

  // ========================================================================
  // STEP 1: Configure the PHYSICAL hardware (TFT_eSPI)
  // This sets up the actual display controller — must happen before LVGL
  // touches the display. The global tft object has board-specific pins
  // pre-configured via user_setup_declared.h.
  // ========================================================================
  Serial.println("[CYD] Step 1: tft.begin()");
  delay(50);
  extern TFT_eSPI tft;  // Declared in project code
  tft.begin();
  Serial.println("[CYD] tft.begin() returned");
  delay(50);

  Serial.println("[CYD] Step 2: colour inversion handled after display init");
  delay(50);

  /* No driver rotation: LVGL creates the display in the panel's native
   * portrait geometry and rotates it with lv_display_set_rotation() below,
   * which is the supported path from LVGL 9.2 on (9.1 compiles the rotation
   * of draw areas out unless LV_DRAW_TRANSFORM_USE_MATRIX is set, which is
   * how the first version of this driver ended up rendering sideways). */
  Serial.println("[CYD] Step 3: driver stays in portrait, LVGL rotates");
  delay(50);

  /* Paint the whole panel once. Right after a power-on reset the ILI9341 GRAM
   * holds undefined content, and LVGL only pushes the areas it marks invalid,
   * so an unpainted panel shows random noise until the first redraw covers
   * it. */
  tft.fillScreen(TFT_BLACK);
  Serial.println("[CYD] panel cleared");
  delay(50);

  // ========================================================================
  // STEP 2: Create the LVGL display abstraction (software layer)
  // This creates an LVGL display object with custom flush callback that
  // delegates to the already-configured physical tft object.
  // ========================================================================
  Serial.println("[CYD] Step 4: lv_tft_espi_create()");
  delay(50);
  /* LVGL's own TFT_eSPI driver: it sets up the display in the panel's native
   * geometry, owns the flush callback, and re-applies tft->setRotation()
   * whenever the resolution changes (i.e. when we call
   * lv_display_set_rotation() below). Buffer geometry and flush areas are
   * guaranteed consistent because both come from the same code. */
  lv_display_t* disp = lv_tft_espi_create(CYD_SCREEN_WIDTH, CYD_SCREEN_HEIGHT,
                                          buffer, buffer_size);
  Serial.printf("[CYD] lv_tft_espi_create returned %p\n", disp);
  delay(50);
  if (disp == NULL) {
    Serial.println("[CYD] ERROR: lv_tft_espi_create returned NULL");
    return NULL;
  }

  /* Map the requested rotation (0-3) to LVGL's. In 9.2 this rotates both the
   * drawing and the areas handed to the driver, so the flush stays correct. */
  lv_display_rotation_t lv_rotation = LV_DISPLAY_ROTATION_0;
  switch (rotation) {
    case 1:  lv_rotation = LV_DISPLAY_ROTATION_90;   break;
    case 2:  lv_rotation = LV_DISPLAY_ROTATION_180;  break;
    case 3:  lv_rotation = LV_DISPLAY_ROTATION_270;  break;
    default: lv_rotation = LV_DISPLAY_ROTATION_0;    break;
  }
  lv_display_set_rotation(disp, lv_rotation);
  Serial.printf("[CYD] lv_display_set_rotation(%d) -> logical %dx%d\n",
                (int)lv_rotation,
                lv_display_get_horizontal_resolution(disp),
                lv_display_get_vertical_resolution(disp));
  delay(50);

  /* This panel comes up colour-inverted (black background renders white, the
   * green status text renders magenta) and only the INVON command (0x21) fixes
   * it — clearing MADCTL bit 3 does not, which was verified by reading MADCTL
   * back. Do the same as the reference CYD setup's TFT_INVERSION_ON, just at
   * runtime so the board files stay free of driver quirks. */
  tpl_cyd_set_inversion(CYD_NEEDS_INVERSION);
  tpl_cyd_apply_inversion();
  Serial.printf("[CYD] colour inversion %s\n",
                tpl_cyd_get_inversion() ? "ON" : "OFF");
  delay(50);

  Serial.println("[CYD] === tpl_cyd_display_init DONE ===");
  g_cyd_display_initialized = true;
  return disp;
}

// ============================================================================
// tpl_cyd_touch_init() — Phase 1.4
// ============================================================================

void tpl_cyd_touch_init(XPT2046_Touchscreen& touchscreen, uint8_t rotation) {
#if CYD_TOUCH_HAS_PRIVATE_SPI
  /* 2.8" board: the XPT2046 sits on its own SPI peripheral (VSPI, since
   * TFT_eSPI is built with USE_HSPI_PORT) with the dedicated pins from
   * tpl_display.h, so it never shares the bus — or the GPIO matrix routing —
   * with the display.
   *
   * The SPIClass must outlive this function: XPT2046_Touchscreen keeps a
   * reference to it and calls beginTransaction()/endTransaction() on every
   * poll. A function-local instance is destroyed on return, and the
   * Arduino SPIClass destructor deletes the FreeRTOS mutex that
   * beginTransaction() takes — the first touch read then aborts with
   * "assert failed: xQueueSemaphoreTake ((pxQueue))". */
  static SPIClass tsSpi(VSPI);
  tsSpi.begin(CYD_TOUCH_CLK, CYD_TOUCH_MISO, CYD_TOUCH_MOSI);
  touchscreen.begin(tsSpi);
  Serial.println("[CYD] 2.8\" CYD — XPT2046 touch on private SPI bus (VSPI)");
#else
  // 3.5" board: shares display SPI — must be called AFTER display init
  if (!g_cyd_display_initialized) {
    Serial.println(
        "[CYD] ERROR: Call tpl_cyd_display_init() before "
        "tpl_cyd_touch_init()");
    Serial.println("[CYD] The 3.5\" board shares the display SPI bus.");
    return;
  }
  // Reuse TFT_eSPI's SPI instance (shared bus)
  touchscreen.begin(TFT_eSPI::getSPIinstance());
  Serial.println("[CYD] 3.5\" CYD — XPT2046 touch on shared display SPI");
#endif

  /* XPT2046 ADC rotation. The display rotation is LVGL's job now (see
   * tpl_cyd_touch_read), so the ADC frame must come out portrait — 2 is the
   * value the reference CYD example uses with these calibration ranges. */
  touchscreen.setRotation(CYD_TOUCH_ADC_ROTATION);
}

// ============================================================================
// tpl_cyd_touch_read() — Phase 1.5 (CORE FUNCTION)
// ============================================================================

void tpl_cyd_touch_read(XPT2046_Touchscreen& touchscreen, lv_indev_data_t* data,
                        bool* pressed, int32_t screen_w, int32_t screen_h,
                        uint8_t rotation) {
  /* LVGL rotates the point it receives from the driver (lv_indev.c,
   * indev_pointer_proc):
   *
   *   ROTATION_0    x_out = x,                              y_out = y
   *   ROTATION_90   x_out = ver_res - y - 1,                y_out = x
   *   ROTATION_180  x_out = hor_res - x - 1,                y_out = ver_res - y - 1
   *   ROTATION_270  x_out = y,                              y_out = hor_res - x - 1
   *
   * so the driver has to report the inverse-transformed point, otherwise
   * every tap lands rotated. That inverse is applied per case below.
   *
   * The ADC mapping itself is the board's verified one: ADC x -> horizontal,
   * ADC y -> vertical, using the calibration ranges in tpl_display.h. In
   * landscape the logical frame is swapped (width = screen_h), so the two
   * ranges land in the opposite order. */
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point raw = touchscreen.getPoint();
#if CYD_TOUCH_SWAP_XY
    // The controller's axes are swapped relative to the panel
    TS_Point p(raw.y, raw.x, raw.z);
#else
    TS_Point p = raw;
#endif

    if (rotation == 1 || rotation == 3) {
      /* Logical landscape: width = screen_h, height = screen_w. map() does
       * not clamp, and an ADC value past the calibration end lands outside
       * the frame — LVGL then logs "X is N which is greater than hor. res". */
      const int32_t land_x =
          constrain(map(p.x, CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX, 0, screen_h - 1),
                    0, screen_h - 1);
      const int32_t land_y =
          constrain(map(p.y, CYD_TOUCH_Y_MIN, CYD_TOUCH_Y_MAX, 0, screen_w - 1),
                    0, screen_w - 1);

      if (rotation == 3) {
        // inverse of: x_out = y, y_out = screen_w - x - 1
        data->point.x = screen_w - 1 - land_y;
        data->point.y = land_x;
      } else {
        // inverse of: x_out = screen_h - y - 1, y_out = x
        data->point.x = land_y;
        data->point.y = screen_h - 1 - land_x;
      }
    } else {
      // Portrait: LVGL applies no transform (rotation 0) or a 180 deg one
      data->point.x =
          constrain(map(p.x, CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX, 1, screen_w), 0,
                      screen_w - 1);
      data->point.y =
          constrain(map(p.y, CYD_TOUCH_Y_MIN, CYD_TOUCH_Y_MAX, 1, screen_h), 0,
                      screen_h - 1);
      if (rotation == 2) {
        data->point.x = screen_w - 1 - data->point.x;
        data->point.y = screen_h - 1 - data->point.y;
      }
    }
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
#endif  // IS_ESP32_CYD_28 || IS_ESP32_CYD_35
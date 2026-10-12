/**
 * @file tpl_display.h
 * @brief CYD (Cheap Yellow Display) board abstraction layer
 *
 * Provides board-specific constants and initialization functions for
 * 2.8″ (SUNTON 2432S028R, ILI9341) and 3.5″ (SUNTON 3248S035R, ST7796)
 * CYD boards. Eliminates hardcoded display/touch configuration from
 * individual project main.cpp files.
 *
 * Usage:
 *   - Define IS_ESP32_CYD_28 or IS_ESP32_CYD_35 in platformio.ini build_flags
 *   - Include this header in your main.cpp
 *   - Declare global: TFT_eSPI tft = TFT_eSPI();
 *   - Call tpl_cyd_display_init() and tpl_cyd_touch_init() in setup()
 *
 * The abstraction handles the full rotation pipeline internally:
 *   Touch ADC → XPT2046 rotation transform → calibration mapping → logical
 * rotation
 *
 * Adding a new CYD variant requires editing ONLY this file + one platformio.ini
 * define.
 */

#ifndef TPL_DISPLAY_H
#define TPL_DISPLAY_H

#include <Arduino.h>

/* CYD-specific headers, constants, and function declarations — only needed
 * when a CYD board is actually selected.  Non-CYD projects that include
 * template.h will never define IS_ESP32_CYD_28 or IS_ESP32_CYD_35, so these
 * libraries stay optional and the header is effectively empty for them. */
#if defined(IS_ESP32_CYD_28) || defined(IS_ESP32_CYD_35)
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// ============================================================================
// Board Detection — exactly one must be defined
// ============================================================================

#if defined(IS_ESP32_CYD_28)

// ============================================================================
// 2.8″ CYD (SUNTON 2432S028R — ILI9341)
// ============================================================================

#define CYD_SCREEN_WIDTH 240
#define CYD_SCREEN_HEIGHT 320
#define CYD_DRIVER ILI9341_2_DRIVER

// Logical dimensions (portrait — rotation handled by tpl_cyd_touch_read)
#define CYD_WIDTH CYD_SCREEN_WIDTH
#define CYD_HEIGHT CYD_SCREEN_HEIGHT

// Display SPI pins (must match the TFT_* -D flags in platformio.ini —
// TFT_eSPI never reads a User_Setup.h, see the note there)
// MOSI 13, MISO 16, SCLK 14, CS 15, DC 2, RST 12, BL 21
// (values used by the known-good boilerplate, see platformio.ini)
#define CYD_TFT_MOSI   13
#define CYD_TFT_MISO   16
#define CYD_TFT_SCLK   14
#define CYD_TFT_CS     15
#define CYD_TFT_DC      2
#define CYD_TFT_RST    12

// Touch (dedicated SPI bus)
#define CYD_TOUCH_MOSI 32
#define CYD_TOUCH_MISO 39
#define CYD_TOUCH_CLK 25
#define CYD_TOUCH_CS 33
#define CYD_TOUCH_IRQ 36
// TOUCH_CS is defined in platformio.ini (CLI define, before any includes)
// "Private" = the touch controller gets the second SPI peripheral (HSPI),
// so TFT_eSPI keeps exclusive use of VSPI.
#define CYD_TOUCH_HAS_PRIVATE_SPI true

// Touch calibration (ADC ranges after XPT2046 setRotation transform)
#define CYD_TOUCH_X_MIN 200
#define CYD_TOUCH_X_MAX 3700
#define CYD_TOUCH_Y_MIN 240
#define CYD_TOUCH_Y_MAX 3800

// Backlight
#define CYD_BACKLIGHT_PIN 21

// RGB LED (inverted logic — HIGH = off)
#define CYD_RGB_RED 4
#define CYD_RGB_GREEN 16
#define CYD_RGB_BLUE 17

// BOOT button (both boards)
#define CYD_BOOT_PIN 0



/* Colour inversion. The 2.8" ILI9341 comes up inverted and needs INVON; the
 * 3.5" ST7796 does not and must NOT have it. A long press on the header band
 * flips it at runtime for boards that differ again. */
#define CYD_NEEDS_INVERSION true

// No speaker or CDS on 2.8″ board
#define CYD_HAS_SPEAKER false
#define CYD_HAS_CDS false

#elif defined(IS_ESP32_CYD_35)

// ============================================================================
// 3.5″ CYD (SUNTON 3248S035R — ST7796)
// ============================================================================

#define CYD_SCREEN_WIDTH 320
#define CYD_SCREEN_HEIGHT 480
#define CYD_DRIVER ST7796_DRIVER

// Logical dimensions (portrait)
#define CYD_WIDTH CYD_SCREEN_WIDTH
#define CYD_HEIGHT CYD_SCREEN_HEIGHT

// Display SPI pins (must match the TFT_* -D flags in platformio.ini)
// SUNTON 3248S035R: MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, no RST pin
#define CYD_TFT_MOSI   13
#define CYD_TFT_MISO   12
#define CYD_TFT_SCLK   14
#define CYD_TFT_CS     15
#define CYD_TFT_DC      2
#define CYD_TFT_RST    -1   // no reset pin — tied to VCC internally

// Touch (shares display SPI — no separate pin defines needed)
#define CYD_TOUCH_CS 33
#define CYD_TOUCH_IRQ 36
// TOUCH_CS is defined in platformio.ini (CLI define, before any includes)
#define CYD_TOUCH_HAS_PRIVATE_SPI false

/* Touch calibration.
 *
 * On this board the controller's axes are swapped relative to the panel: ADC y
 * drives the panel's horizontal axis and ADC x its vertical one (verified with
 * labelled corner taps — the toggle band appeared on a column while the values
 * were otherwise in range). The ranges are therefore stated in *panel* terms —
 * X applies to the swapped-in ADC y, Y to ADC x — and both run low-to-high.
 */
#define CYD_TOUCH_SWAP_XY true
#define CYD_TOUCH_X_MIN  180
#define CYD_TOUCH_X_MAX  3800
#define CYD_TOUCH_Y_MIN  220
#define CYD_TOUCH_Y_MAX  3900

// Backlight
#define CYD_BACKLIGHT_PIN 27  // differs from 2.8″

// The 3.5" ST7796 shows correct colours without INVON
#define CYD_NEEDS_INVERSION false

// Speaker (3.5″ only)
#define CYD_SPEAKER_PIN 26    // DAC2 / I2S
#define CYD_HAS_SPEAKER true

// CDS light sensor (3.5″ only)
#define CYD_CDS_PIN 34  // analog input (Cadmium Sulfide photoresistor)
#define CYD_HAS_CDS true

// RGB LED (floating pins — HIGH = prevent floating)
#define CYD_RGB_RED 4
#define CYD_RGB_GREEN 16
#define CYD_RGB_BLUE 17

// BOOT button (both boards)
#define CYD_BOOT_PIN 0

#endif  // IS_ESP32_CYD_35

/* XPT2046 ADC rotation. 3 is the value the mapping in tpl_cyd_touch_read()
 * was verified with on the 2.8" board. */
#ifndef CYD_TOUCH_ADC_ROTATION
#define CYD_TOUCH_ADC_ROTATION 3
#endif

/* True when the touch controller's ADC axes are swapped relative to the panel
 * (ADC y drives the panel's horizontal axis). Verified on the 3.5", where the
 * panel's ADC y range is what tracks the horizontal axis. */
#ifndef CYD_TOUCH_SWAP_XY
#define CYD_TOUCH_SWAP_XY false
#endif

// ============================================================================
// TFT_eSPI configuration
// ============================================================================
// These used to live here, but TFT_eSPI includes User_Setup.h *before* this
// header is ever seen, so they had no effect on the library (which silently
// fell back to its stock ESP8266/NodeMCU defaults). TFT_eSPI is now configured
// entirely from -D flags in each project's platformio.ini (USER_SETUP_LOADED,
// TFT_MOSI/MISO/SCLK/CS/DC/RST, driver, TFT_WIDTH/TFT_HEIGHT). The constants
// above document those values; keep both in sync.

// Draw buffer size macro (depends on resolution)
#define CYD_DRAW_BUF_SIZE \
  (CYD_SCREEN_WIDTH * CYD_SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

// ============================================================================
// Function Declarations — only when a CYD board is selected
// ============================================================================

/**
 * Initialize TFT_eSPI and LVGL display driver for CYD boards.
 *
 * Uses the global TFT_eSPI tft object (declared in project code) which has
 * board-specific pins configured via the TFT_* -D flags in platformio.ini.
 *
 * @param rotation Display rotation (0–3). The LVGL display is created with the
 *                 already-rotated dimensions (240x320 in portrait, 320x240 in
 *                 landscape); the physical rotation is applied by TFT_eSPI.
 *                 lv_display_set_rotation() is deliberately NOT used — it only
 *                 works with LV_DRAW_TRANSFORM_USE_MATRIX == 1 and would
 *                 otherwise desynchronise the draw buffer stride from the
 *                 resolution LVGL lays out with.
 * @param buffer   Draw buffer (uint32_t array, CYD_DRAW_BUF_SIZE / 4 elements)
 * @param buf_size Size of buffer in bytes (sizeof(draw_buf))
 * @return lv_display_t* pointer, or NULL on failure
 *
 * The caller must:
 *   - Declare global: TFT_eSPI tft = TFT_eSPI();
 *   - Declare a draw buffer: uint32_t draw_buf[CYD_DRAW_BUF_SIZE / 4];
 *   - Call lv_display_set_default() with the returned pointer (LVGL 9.x
 *     requires the default display to be set before lv_indev_create() or
 *     any UI code can find the display).
 *   - Create an input device with lv_indev_create()
 *   - Register a touch read callback via lv_indev_set_read_cb()
 */
lv_display_t* tpl_cyd_display_init(uint8_t rotation, void* buffer,
                                   uint32_t buffer_size);

/**
 * Turn the panel's colour inversion on or off and remember the state.
 *
 * The 2.8" ILI9341 comes up inverted and only INVON (0x21) corrects it; the
 * 3.5" ST7796 must be left alone. The default comes from the board
 * (CYD_NEEDS_INVERSION). Projects can flip it at runtime — e.g. a long press on
 * the header band — instead of paying for a reflash per attempt.
 */
void tpl_cyd_set_inversion(bool on);

/** Colour inversion state currently requested for the panel. */
bool tpl_cyd_get_inversion(void);

/**
 * Send a pending inversion change to the panel.
 *
 * Call this from loop(), outside lv_task_handler(): the LVGL driver's
 * TFT_eSPI instance holds an SPI transaction during a flush, and a command
 * sent inside that window is silently lost. tpl_cyd_display_init() applies the
 * initial state itself, so calling it once per loop iteration is enough.
 */
void tpl_cyd_apply_inversion(void);

/**
 * Initialize the XPT2046 touchscreen.
 *
 * Must be called AFTER tpl_cyd_display_init() — the 3.5" board
 * shares the display SPI bus, and the SPI must be routed first.
 * On 2.8" boards (private SPI), the order is irrelevant.
 *
 * @param touchscreen Reference to XPT2046_Touchscreen object (must be
 * constructed)
 * @param rotation Must match the display rotation passed to
 * tpl_cyd_display_init()
 *
 * Sets up the SPI bus (private or shared) and calls setRotation().
 * For 3.5" boards, reuses TFT_eSPI's SPI instance (getSPIinstance()).
 *
 * Returns void. On 3.5" boards, calls tpl_cyd_display_init() first
 * with an assert/serial message if that hasn't happened yet.
 */
void tpl_cyd_touch_init(XPT2046_Touchscreen& touchscreen, uint8_t rotation);

/**
 * Read touch data and map to screen coordinates in the requested orientation.
 *
 * This is the core of the abstraction: it handles the full rotation pipeline
 * internally, so project code is rotation-agnostic.
 *
 * Pipeline:
 *   1. Calls touchscreen.getPoint() for raw ADC values
 *   2. Applies XPT2046 library rotation transform
 *   3. Maps rotated ADC to portrait screen coordinates using calibration ranges
 *   4. Applies the requested logical rotation
 *   5. Returns coordinates in the requested orientation, ready for LVGL
 *
 * @param touchscreen Reference to XPT2046_Touchscreen object
 * @param data Pointer to lv_indev_data_t (data->point will be set)
 * @param pressed Set to true if touch detected, false if released
 * @param screen_w Screen width (portrait, from CYD_SCREEN_WIDTH)
 * @param screen_h Screen height (portrait, from CYD_SCREEN_HEIGHT)
 * @param rotation Display rotation (0–3, must match display init)
 */
void tpl_cyd_touch_read(XPT2046_Touchscreen& touchscreen, lv_indev_data_t* data,
                        bool* pressed, int32_t screen_w, int32_t screen_h,
                        uint8_t rotation);

/**
 * Initialize backlight PWM for CYD boards.
 *
 * @param pwm_channel LEDC channel to use (7 recommended)
 * @param initial_brightness 0-255
 */
void tpl_cyd_backlight_init(int pwm_channel, uint8_t initial_brightness);

/**
 * Initialize RGB LED pins for CYD boards.
 *
 * For 2.8" boards, configures CYD_RGB_RED/GREEN/BLUE as outputs
 * and sets them HIGH (inverted logic → LEDs off).
 * For 3.5" boards, sets pins 4/16/17 as outputs and pulls them HIGH
 * to prevent floating input state.
 *
 * Same pin state (HIGH), different hardware reasons — works for both.
 *
 * @param turn_off_rgb true to set all RGB pins to OFF state
 */
void tpl_cyd_rgb_init(bool turn_off_rgb);

/**
 * Read the CDS (Cadmium Sulfide) light sensor on 3.5" CYD boards.
 *
 * Only meaningful on 3.5" boards — the 2.8" board has no CDS component.
 * Returns 0–4095 from analogRead(CYD_CDS_PIN).
 *
 * @return ADC value (0–4095), or 0 if not on a 3.5" board
 */
uint16_t tpl_cyd_light_sensor_read(void);

#endif  // IS_ESP32_CYD_28 || IS_ESP32_CYD_35

#endif  // TPL_DISPLAY_H
/**
 * @file main.cpp
 * @brief 28_hauszentrale — WAVESHARE ESP32-S3 LCD 7" panel.
 *
 * S4 · RGB panel + LVGL display (already implemented in tpl_ws7_display_init)
 * S10 · Home screen — wired from haus_ui.c + haus_gui.cpp
 *
 * Boot sequence:
 *   1. System init (FreeRTOS task management)
 *   2. CH422G expander + panel reset
 *   3. GT911 touch
 *   4. RGB panel + LVGL display
 *   5. Build home screen (haus_ui_init)
 *   6. WiFi + webserver + websocket
 *   7. Loop: lv_task_handler + gui_update_state + haus_ui_update
 */

#include "template.h"
#include "haus_gui.h"
#include "haus_ui.h"
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <esp_timer.h>
#include <lvgl.h>

/* S4.03: RGB565 draw buffer in internal RAM (not PSRAM) */
static uint8_t s_draw_buf0[800 * 20 * 2];

/* LVGL 9.2 needs a millisecond tick source */
static uint32_t lv_tick_cb(void) { return (uint32_t)millis(); }

static lv_display_t *s_disp = NULL;

/* ─── Rotation helper (S4.10 — kept for testing, not used by home screen) ── */

static lv_display_rotation_t rot_to_lvgl(int r) {
    switch (r) {
        case 0:  return LV_DISPLAY_ROTATION_90;
        case 1:  return LV_DISPLAY_ROTATION_0;
        case 2:  return LV_DISPLAY_ROTATION_270;
        case 3:  return LV_DISPLAY_ROTATION_180;
        default: return LV_DISPLAY_ROTATION_90;
    }
}

/* ─── Setup ──────────────────────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("28_hauszentrale S10 — home screen");

    tpl_system_setup(0);  // no deep sleep

    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    Serial.printf("Flash: %d MB, PSRAM: %s\n",
                  flash_size / 1024 / 1024,
                  psramFound() ? "8 MB" : "0 MB");

    /* CH422G expander + keep backlight off until the panel is up */
    tpl_ws7_expander_init();
    tpl_ws7_backlight(false);

    /* S4.02: reset the ST7262 through the CH422G before bring-up */
    tpl_ws7_panel_reset();

    /* S5.01–S5.05: GT911 touch initialization */
    const bool touch_ok = (tpl_ws7_touch_init() == ESP_OK);
    Serial.printf("GT911 touch: %s\n", touch_ok ? "ready" : "NOT FOUND (touch disabled)");

    /* S4.01–S4.05: RGB panel + LVGL display */
    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    s_disp = tpl_ws7_display_init(TPL_WS7_ROTATION, (uint32_t *)s_draw_buf0, sizeof(s_draw_buf0));
    if (s_disp) {
        Serial.println("RGB panel + LVGL display initialized");

        /* S5.03: register the GT911 as the pointer input device */
        if (touch_ok) {
            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, tpl_ws7_touch_read_cb);
            lv_indev_set_display(indev, s_disp);

            /* Visible cursor follows the finger for calibration */
            lv_obj_t *cursor = lv_obj_create(lv_layer_sys());
            lv_obj_remove_style_all(cursor);
            lv_obj_set_size(cursor, 24, 24);
            lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFF0000), 0);
            lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
            lv_indev_set_cursor(indev, cursor);
        }

        /* ── S10: Build the home screen ─────────────────────────────── */
        haus_ui_init();
        Serial.println("S10: home screen built");

        /* Render the first full frame before lighting the panel */
        int64_t flush_start = esp_timer_get_time();
        lv_refr_now(s_disp);
        int64_t flush_us = esp_timer_get_time() - flush_start;
        Serial.printf("S4.11: full-frame flush = %lld us (%.1f ms)\n",
                      flush_us, flush_us / 1000.0);

        tpl_ws7_backlight(true);
        Serial.println("S10: home screen active — data shows — (S9/S17 stubs)");
    } else {
        Serial.println("ERROR: RGB panel initialization failed");
    }

    /* WiFi + webserver + websocket (standard template setup) */
    tpl_wifi_setup(true, true, (gpio_num_t)255);  // no status LED on this board
    tpl_webserver_setup();
    tpl_websocket_setup(NULL, NULL);
#ifdef NET_WATCHDOG
    tpl_net_watchdog_setup();
#endif
    tpl_command_setup(NULL);
}

/* ─── Loop ───────────────────────────────────────────────────────────────── */

void loop() {
    /* Update GUI data from CAN/SD/NTP (stub — returns "no data" until S9/S17) */
    gui_update_state();

    /* Refresh home screen from gui_get_state() */
    haus_ui_update();

    /* Process LVGL tasks (display flush, input processing, animations) */
    lv_task_handler();

    vTaskDelay(5 / portTICK_PERIOD_MS);
}

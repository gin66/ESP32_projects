#include "template.h"
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <esp_timer.h>
#include <lvgl.h>

/* S4.03: RGB565 draw buffer in internal RAM (not PSRAM) */
static uint8_t s_draw_buf0[800 * 20 * 2];

/* LVGL 9.2 needs a millisecond tick source */
static uint32_t lv_tick_cb(void) { return (uint32_t)millis(); }

static lv_display_t *s_disp = NULL;

/* Project rotation index (§6.6) -> LVGL enum. Index 0 is portrait and must NOT
 * be LVGL rotation 0, which would leave the panel in landscape. */
static lv_display_rotation_t rot_to_lvgl(int r) {
    switch (r) {
        case 0:  return LV_DISPLAY_ROTATION_90;   /* portrait */
        case 1:  return LV_DISPLAY_ROTATION_0;    /* landscape */
        case 2:  return LV_DISPLAY_ROTATION_270;  /* portrait, 180° */
        case 3:  return LV_DISPLAY_ROTATION_180;  /* landscape, mirrored */
        default: return LV_DISPLAY_ROTATION_90;
    }
}

/* Build the test pattern in LOGICAL (rotated) coordinates. LVGL lays out and
 * renders in logical space; ws7_rgb_flush_cb() rotates to native panel pixels.
 * Rotation 0 => 480x800, rotation 1/3 => 800x480. */
static void build_test_screen(void) {
    const int32_t w = lv_display_get_horizontal_resolution(s_disp);
    const int32_t h = lv_display_get_vertical_resolution(s_disp);
    Serial.printf("S4.09/S4.10: logical %dx%d, lvgl rotation %d\n",
                  (int)w, (int)h, (int)lv_display_get_rotation(s_disp));

    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const int nbars = 6;
    const char *rnames[4] = {"0: portrait", "1: landscape", "2: portrait", "3: landscape"};

    /* Top half: six horizontal colour bars spanning the full logical width. */
    const int32_t top_h = h / 2;
    lv_color_t colors[nbars] = {
        lv_color_hex(0xFF0000),  /* Red */
        lv_color_hex(0x00FF00),  /* Green */
        lv_color_hex(0x0000FF),  /* Blue */
        lv_color_hex(0xFFFF00),  /* Yellow */
        lv_color_hex(0xFF00FF),  /* Magenta */
        lv_color_hex(0x00FFFF),  /* Cyan */
    };
    for (int i = 0; i < nbars; i++) {
        int32_t y0 = (top_h * i) / nbars;
        int32_t y1 = (top_h * (i + 1)) / nbars;
        lv_obj_t *rect = lv_obj_create(scr);
        lv_obj_remove_style_all(rect);
        lv_obj_set_size(rect, w, y1 - y0);
        lv_obj_set_pos(rect, 0, y0);
        lv_obj_set_style_bg_color(rect, colors[i], 0);
        lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
        lv_obj_clear_flag(rect, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Bottom half: four tappable rotation zones. */
    const int32_t bot_h = h - top_h;
    for (int r = 0; r < 4; r++) {
        int32_t y0 = top_h + (bot_h * r) / 4;
        int32_t y1 = top_h + (bot_h * (r + 1)) / 4;
        lv_obj_t *zone = lv_obj_create(scr);
        lv_obj_remove_style_all(zone);
        lv_obj_set_size(zone, w, y1 - y0);
        lv_obj_set_pos(zone, 0, y0);
        lv_obj_set_style_bg_color(zone, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(zone, LV_OPA_COVER, 0);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(zone, (void *)(uintptr_t)r);

        lv_obj_t *label = lv_label_create(zone);
        lv_label_set_text(label, rnames[r]);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);

        lv_obj_add_event_cb(zone, [](lv_event_t *ev) {
            lv_obj_t *z = (lv_obj_t *)lv_event_get_target(ev);
            int r = (int)(uintptr_t)lv_obj_get_user_data(z);
            if (r < 0 || r >= 4) return;
            /* Switch rotation on the existing display; do NOT create a new
             * panel/display (that leaked the RGB panel and detached the indev). */
            lv_display_set_rotation(s_disp, rot_to_lvgl(r));
            Serial.printf("S4.10: rotated to %d\n", r);
            build_test_screen();
            lv_refr_now(s_disp);
        }, LV_EVENT_CLICKED, NULL);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("28_hauszentrale S1.01 OK");

    tpl_system_setup(0);  // no deep sleep

    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    Serial.printf("Flash: %d MB, PSRAM: %s\n",
                  flash_size / 1024 / 1024,
                  psramFound() ? "8 MB" : "0 MB");

    tpl_ws7_expander_init();
    tpl_ws7_backlight(false);  // keep backlight off until the panel is up

    /* S4.02: reset the ST7262 through the CH422G before bring-up */
    tpl_ws7_panel_reset();

    /* S5.01-S5.05: GT911 touch initialization */
    const bool touch_ok = (tpl_ws7_touch_init() == ESP_OK);
    Serial.printf("GT911 touch: %s\n", touch_ok ? "ready" : "NOT FOUND (touch disabled)");

    /* S4.01-S4.05: RGB panel + LVGL display */
    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    s_disp = tpl_ws7_display_init(TPL_WS7_ROTATION, (uint32_t *)s_draw_buf0, sizeof(s_draw_buf0));
    if (s_disp) {
        Serial.println("RGB panel + LVGL display initialized");

        /* S5.03: register the GT911 as the pointer input device. Only when the
         * controller answered on the bus, otherwise the poll floods the log. */
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

        build_test_screen();

        /* Render the first full frame before lighting the panel. */
        int64_t flush_start = esp_timer_get_time();
        lv_refr_now(s_disp);
        int64_t flush_us = esp_timer_get_time() - flush_start;
        Serial.printf("S4.11: full-frame flush = %lld us (%.1f ms)\n",
                      flush_us, flush_us / 1000.0);
        Serial.println("S4.10: tap a rotation label zone to switch orientation");

        tpl_ws7_backlight(true);

        Serial.println("Color bars + rotation labels drawn");
    } else {
        Serial.println("ERROR: RGB panel initialization failed");
    }

    tpl_wifi_setup(true, true, (gpio_num_t)255);  // no status LED on this board
    tpl_webserver_setup();
    tpl_websocket_setup(NULL, NULL);
#ifdef NET_WATCHDOG
    tpl_net_watchdog_setup();
#endif
    tpl_command_setup(NULL);
}

void loop() {
    lv_task_handler();
    vTaskDelay(5 / portTICK_PERIOD_MS);
}

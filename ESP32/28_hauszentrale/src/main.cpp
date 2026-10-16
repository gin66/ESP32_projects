#include "template.h"
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <lvgl.h>

/* S4.03: Two 800x20 RGB565 draw buffers in internal RAM (not PSRAM) */
static uint8_t s_draw_buf0[800 * 20 * 2];
static uint8_t s_draw_buf1[800 * 20 * 2];

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
    tpl_ws7_backlight(false);  // turn off backlight

    /* S5.01-S5.05: GT911 touch initialization */
    tpl_ws7_touch_init();
    Serial.println("GT911 touch initialized");

    /* S4.01-S4.05: RGB panel + LVGL display */
    lv_display_t *disp = tpl_ws7_display_init(0, (uint32_t *)s_draw_buf0, sizeof(s_draw_buf0));
    if (disp) {
        Serial.println("RGB panel + LVGL display initialized");
        
        /* S4.09: Color bar test pattern */
        Serial.println("S4.09: Color bar test pattern");
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        
        /* Draw horizontal color bars (top 400px) */
        uint16_t bar_h = 400 / 6;
        lv_color_t colors[] = {
            lv_color_hex(0xFF0000),  /* Red */
            lv_color_hex(0x00FF00),  /* Green */
            lv_color_hex(0x0000FF),  /* Blue */
            lv_color_hex(0xFFFF00),  /* Yellow */
            lv_color_hex(0xFF00FF),  /* Magenta */
            lv_color_hex(0x00FFFF),  /* Cyan */
        };
        
        for (int i = 0; i < 6; i++) {
            lv_obj_t *rect = lv_obj_create(scr);
            lv_obj_set_size(rect, 800, bar_h);
            lv_obj_set_pos(rect, 0, i * bar_h);
            lv_obj_set_style_bg_color(rect, colors[i], 0);
            lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
            lv_obj_remove_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
        }
        
        /* Bottom 80px: status text */
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "S4.09 Color bar test - check orientation");
        lv_obj_set_pos(label, 100, 410);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        
        Serial.println("Color bars drawn — verify correct colors and orientation");
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
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

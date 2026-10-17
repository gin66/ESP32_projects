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

#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <time.h>

#include <cassert>

#include "haus_can_bus.h"
#include "haus_gui.h"
#include "haus_history.h"
#include "haus_sd.h"
#include "haus_sht45.h"
#include "haus_ui.h"
#include "template.h"

/* S4.03: RGB565 draw buffer in internal RAM (not PSRAM) */
static uint8_t s_draw_buf0[800 * 20 * 2];

/* LVGL 9.2 needs a millisecond tick source */
static uint32_t lv_tick_cb(void) { return (uint32_t)millis(); }

static lv_display_t* s_disp = NULL;

/* Calibration cursor: shown while a finger is down, hidden one second after
 * release so it never stays on the wall panel. */
static lv_indev_t* s_touch_indev = NULL;
static lv_obj_t* s_touch_cursor = NULL;
static uint32_t s_last_touch_ms = 0;
#define TOUCH_CURSOR_HIDE_MS 1000

/* ─── Rotation helper (S4.10 — kept for testing, not used by home screen) ── */

static lv_display_rotation_t rot_to_lvgl(int r) {
  switch (r) {
    case 0:
      return LV_DISPLAY_ROTATION_90;
    case 1:
      return LV_DISPLAY_ROTATION_0;
    case 2:
      return LV_DISPLAY_ROTATION_270;
    case 3:
      return LV_DISPLAY_ROTATION_180;
    default:
      return LV_DISPLAY_ROTATION_90;
  }
}

/* ─── Setup ──────────────────────────────────────────────────────────────── */

void setup() {
  Serial.begin(115200);
  delay(100);

  /* Diagnostics first: everything after this point reaches the PSRAM ring,
   * the WebSocket sink and /log. */
  tpl_debug_log_setup();
  TPL_LOGI("boot", "28_hauszentrale starting");

  tpl_system_setup(0);  // no deep sleep

  /* S26.02/S26.03: heap baseline for the 64 KB budget check. */
  const size_t heap_base = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t psram_base = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  TPL_LOGI("mem", "S26 baseline: heap %u B, PSRAM %u B", (unsigned)heap_base,
           (unsigned)psram_base);

  size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint32_t flash_size = 0;
  esp_flash_get_size(NULL, &flash_size);
  TPL_LOGI("boot", "Flash: %d MB, PSRAM: %s\n", flash_size / 1024 / 1024,
           psramFound() ? "8 MB" : "0 MB");

  /* CH422G expander + keep backlight off until the panel is up */
  tpl_ws7_expander_init();
  /* S24.01/S24.02: USB_SEL low unconditionally before anything else, so an
   * unconfigured panel always keeps its CH9102 console. */
  tpl_ws7_set_usb_mode(true);
  tpl_ws7_backlight(false);

  /* Local climate: Sensirion SHT45 at 0x44 on the same I2C bus. Missing
   * hardware is not fatal — the living-room zone then stays "--". */
  if (!haus_sht45_init()) {
    TPL_LOGW("climate", "SHT45 not found; living-room values unavailable");
  }

  /* S11.00: SD bring-up through CH422G IO4 + SPI3 pins. Failure is logged
   * but never blocks the panel. */
  if (haus_sd_mount()) {
    haus_sd_selftest();
    /* S26.02: heap after the first SD write (the mount self-test). */
    TPL_LOGI("mem", "S26.02 after first SD write: heap %u B (delta %d B)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)((int)heap_base -
                   (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else {
    /* Double bookkeeping: losing the card costs minute detail, not the
     * hourly history, which is written independently to SPIFFS. */
    TPL_LOGW("sd",
             "SD unavailable; minute history off, hourly SPIFFS "
             "bookkeeping continues");
  }

  /* L1/L2 plus the independent SPIFFS hourly ring. Failure here is logged,
   * but the panel still boots. */
  if (!haus_history_init()) {
    TPL_LOGW("history", "history store unavailable");
  } else {
    /* S11.08/S11.09: restore before the first frame is ever drawn. */
    const size_t restored = haus_history_restore((uint32_t)time(nullptr));
    TPL_LOGI("history", "boot restore: %u columns", (unsigned)restored);
    haus_history_start_recorder();
  }

  /* S4.02: reset the ST7262 through the CH422G before bring-up */
  tpl_ws7_panel_reset();

  /* S5.01–S5.05: GT911 touch initialization */
  const bool touch_ok = (tpl_ws7_touch_init() == ESP_OK);
  TPL_LOGI("boot", "GT911 touch: %s\n",
           touch_ok ? "ready" : "NOT FOUND (touch disabled)");

  /* S4.01–S4.05: RGB panel + LVGL display */
  lv_init();
  /* LVGL's own globals are part of the baseline, not of the per-screen heap
   * budget (N6 counts the transient allocations below). */
  const size_t heap_after_lv = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  lv_tick_set_cb(lv_tick_cb);
  /* S26.01: the panel DMA cannot cross the PSRAM boundary. */
  assert(esp_ptr_internal(s_draw_buf0));
  TPL_LOGI("mem", "S26.01 draw buffer %p: internal=%s", (void*)s_draw_buf0,
           esp_ptr_internal(s_draw_buf0) ? "yes" : "NO");
  s_disp = tpl_ws7_display_init(TPL_WS7_ROTATION, (uint32_t*)s_draw_buf0,
                                sizeof(s_draw_buf0));
  if (s_disp) {
    TPL_LOGI("boot", "RGB panel + LVGL display initialized");

    /* S5.03: register the GT911 as the pointer input device */
    if (touch_ok) {
      lv_indev_t* indev = lv_indev_create();
      lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
      lv_indev_set_read_cb(indev, tpl_ws7_touch_read_cb);
      lv_indev_set_display(indev, s_disp);

      /* Visible cursor follows the finger; it is hidden again after a
       * second without a press. */
      lv_obj_t* cursor = lv_obj_create(lv_layer_sys());
      lv_obj_remove_style_all(cursor);
      lv_obj_set_size(cursor, 24, 24);
      lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFF0000), 0);
      lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
      lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
      lv_indev_set_cursor(indev, cursor);

      s_touch_indev = indev;
      s_touch_cursor = cursor;
    }

    /* ── S10: Build the home screen ─────────────────────────────── */
    haus_ui_init();
    TPL_LOGI("boot", "S10: home screen built");

    /* Render the first full frame before lighting the panel */
    int64_t flush_start = esp_timer_get_time();
    lv_refr_now(s_disp);
    int64_t flush_us = esp_timer_get_time() - flush_start;
    TPL_LOGI("boot", "S4.11: full-frame flush = %lld us (%.1f ms)\n", flush_us,
             flush_us / 1000.0);

    tpl_ws7_backlight(true);
    TPL_LOGI("boot", "S10: home screen active — data shows — (S9/S17 stubs)");
    TPL_LOGI("boot", "home screen active, flush %lld us", flush_us);

    /* S26.02/S26.03: heap after ui_init; transient allocations must stay
     * under the 64 KB budget (N6). */
    const size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const int heap_delta = (int)heap_after_lv - (int)heap_after;
    const int lvlg_boot_delta = (int)heap_base - (int)heap_after_lv;
    TPL_LOGI("mem",
             "S26.03 after ui_init: heap %u B (ui delta %d B, lv_init %d B)",
             (unsigned)heap_after, heap_delta, lvlg_boot_delta);
    if (heap_delta > 64 * 1024)
      TPL_LOGE("mem", "S26.03 ui heap delta %d B > 64 KB", heap_delta);

    /* S26.04: PSRAM use after ui_init (background decode + LVGL objects).
     * The §10.5.1 table expects roughly 1.3 MB committed at this point and
     * more than 5 MB of headroom. */
    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    TPL_LOGI("mem", "S26.04 PSRAM free %u B (delta %d B), headroom > 4 MB: %s",
             (unsigned)psram_after, (int)psram_base - (int)psram_after,
             psram_after > (4u * 1024u * 1024u) ? "yes" : "NO");
    if (psram_after <= (4u * 1024u * 1024u))
      TPL_LOGE("mem", "S26.04 PSRAM headroom below 4 MB");
    assert(psram_after > (4u * 1024u * 1024u));
  } else {
    TPL_LOGI("boot", "ERROR: RGB panel initialization failed");
    TPL_LOGE("boot", "RGB panel initialization failed");
  }

  /* WiFi + webserver + websocket (standard template setup). The panel is
   * already running at this point; this only enables the optional network
   * services and the initial clock sync. */
  tpl_wifi_setup(true, true, (gpio_num_t)255);   // no status LED on this board
  sntp_set_sync_interval(60UL * 60UL * 1000UL);  // refresh once an hour
  tpl_webserver_setup();
  tpl_debug_log_register_endpoints();
  /* S23.14: /sd/status with the same figures the storage panel shows. */
  tpl_server.on("/sd/status", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    gui_storage_refresh();
    HausState st;
    gui_get_state(&st);
    haus_can_snapshot_t cs;
    haus_can_bus_get(&cs);
    const uint64_t total = st.can.sd_total_bytes;
    const uint64_t used = st.can.sd_used_bytes;
    const uint64_t free_bytes = total > used ? total - used : 0;
    char buf[640];
    snprintf(buf, sizeof(buf),
             "{\"initialized\":%s,\"type\":\"%s\",\"total\":%llu,"
             "\"used\":%llu,\"free\":%llu,\"floor\":%llu,"
             "\"headroom\":%llu,\"samples\":%llu,\"rollup\":%llu,"
             "\"logs\":%llu,\"last_rollover\":%lu,\"newest_file\":%lu,"
             "\"fault_since\":%lu,\"writing\":%s,"
             "\"can_running\":%s,\"mux_can\":%s,"
             "\"can_state\":%u,\"can_tec\":%lu,\"can_rec\":%lu,"
             "\"can_rx\":%lu,\"can_tx\":%lu,\"can_err\":%lu,"
             "\"can_fault\":%s}",
             strcmp(st.can.sd_card_type, "ABSENT") == 0 ? "false" : "true",
             st.can.sd_card_type, (unsigned long long)total,
             (unsigned long long)used, (unsigned long long)free_bytes,
             (unsigned long long)HAUS_FREE_FLOOR_BYTES,
             (unsigned long long)(free_bytes > HAUS_FREE_FLOOR_BYTES
                                      ? free_bytes - HAUS_FREE_FLOOR_BYTES
                                      : 0),
             (unsigned long long)st.can.sd_samples_bytes,
             (unsigned long long)st.can.sd_rollup_bytes,
             (unsigned long long)st.can.sd_logs_bytes,
             (unsigned long)st.can.sd_last_rollover_epoch,
             (unsigned long)st.can.sd_newest_file_epoch,
             (unsigned long)st.can.sd_fault_since_epoch,
             st.can.sd_writing ? "true" : "false",
             haus_can_bus_running() ? "true" : "false",
             tpl_ws7_can_available() ? "true" : "false",
             (unsigned)cs.twai_state, (unsigned long)cs.tx_error_counter,
             (unsigned long)cs.rx_error_counter,
             (unsigned long)cs.rx_frames, (unsigned long)cs.tx_frames,
             (unsigned long)cs.bus_errors,
             cs.bus_fault ? "true" : "false");
    tpl_server.send(200, "application/json", buf);
  });
  tpl_websocket_setup(NULL, NULL);
  /* No net watchdog: WiFi is optional after boot. The WiFi manager keeps
   * retrying the link, while the panel stays fully operational offline. */
  tpl_command_setup(NULL);

#ifdef HAUS_CAN_BOOT_ENABLE
  /* Bench bring-up: CAN starts 20 s after boot (see loop), leaving a window
   * for USB flashing/monitoring. S24 replaces this with the persisted config.
   */
  TPL_LOGI("can", "CAN will start 20 s after boot");
#endif
}

/* ─── Loop ───────────────────────────────────────────────────────────────── */

void loop() {
  static uint32_t last_ui_ms = 0;
  static uint32_t last_anim_ms = 0;
  static bool s_ota_backlight_off = false;
  const uint32_t now = millis();

#ifdef HAUS_CAN_BOOT_ENABLE
  /* Bench: keep the USB console for 20 s, then hand GPIO 19/20 to CAN. */
  static bool can_started = false;
  if (!can_started && now > 20000u) {
    can_started = true;
    gui_set_can_enabled(true);
    TPL_LOGI("can", "CAN after 20 s: %s",
             haus_can_bus_running() ? "running" : "failed");
  }
#endif

  /* OTA writes flash with the cache/PSRAM disabled while the RGB DMA is still
   * scanning the PSRAM framebuffer, which garbles the panel completely. Blank
   * the backlight for the duration; on a failed update restore it. */
  if (tpl_config.ota_ongoing != s_ota_backlight_off) {
    s_ota_backlight_off = tpl_config.ota_ongoing;
    tpl_ws7_backlight(!s_ota_backlight_off);
    TPL_LOGI("ota", "backlight %s for OTA",
             s_ota_backlight_off ? "OFF" : "ON");
  }

  /* Data refresh at 1 Hz. Refreshing at loop rate invalidates every label
   * hundreds of times per second, which the single-buffered RGB panel shows
   * as flicker and tearing. */
  if (now - last_ui_ms >= 1000) {
    last_ui_ms = now;
    gui_update_state();
    haus_ui_update();
  }

  /* Flame breathing at 20 Hz; it only touches opacity when it changes. */
  if (now - last_anim_ms >= 50) {
    last_anim_ms = now;
    haus_ui_animate();
  }

  if (tpl_ws7_touch_points() >= 3) {
    haus_ui_three_finger();
  }

  /* Backlight recovery: with the panel dark the settings row cannot be found,
   * so the next touch anywhere turns the backlight back on. The tap also
   * reaches the widget under it, which is harmless. */
  if (s_touch_indev && !gui_backlight_on() &&
      lv_indev_get_state(s_touch_indev) == LV_INDEV_STATE_PRESSED) {
    gui_set_backlight(true);
  }

  /* Calibration cursor: visible while pressed, auto-hidden after release. */
  if (s_touch_cursor && s_touch_indev) {
    if (lv_indev_get_state(s_touch_indev) == LV_INDEV_STATE_PRESSED) {
      s_last_touch_ms = now;
      lv_obj_remove_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN);
    } else if (!lv_obj_has_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN) &&
               (now - s_last_touch_ms) >= TOUCH_CURSOR_HIDE_MS) {
      lv_obj_add_flag(s_touch_cursor, LV_OBJ_FLAG_HIDDEN);
    }
  }

  /* Process LVGL tasks (display flush, input processing, animations) */
  lv_task_handler();

  vTaskDelay(5 / portTICK_PERIOD_MS);
}

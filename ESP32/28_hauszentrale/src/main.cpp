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
#include "haus_config.h"
#include "haus_gui.h"
#include "haus_history.h"
#include "haus_sd.h"
#include "haus_sensor_roles.h"
#include "haus_sht45.h"
#include "haus_touch_test.h"
#include "haus_ui.h"
#include "template.h"

/* S4.03: RGB565 draw buffer in internal RAM (not PSRAM). Ten lines instead of
 * twenty returns 16 KB before main() runs (docs/memory_report.md, S28.12). */
static uint8_t s_draw_buf0[TPL_WS7_SCREEN_WIDTH * TPL_WS7_DRAW_LINES * 2];

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

/* Loop cadence diagnostics (S17.07): ui_hz is the LVGL handler rate, ui_gap_ms
 * the longest gap between iterations in the last window (includes the 1 Hz
 * full-frame flush). A CAN flood must not degrade either. */
static bool s_can_start_pending = false;
static volatile uint32_t s_ui_hz;
static volatile uint32_t s_ui_gap_ms;

/* S28.03/S28.04: one acceptance threshold for internal RAM. Below it, a
 * later fopen(), HTTP request or mDNS packet can abort the chip. Log loudly
 * instead of discovering it in lock_init_generic. */
static bool internal_heap_ok(const char* point) {
  const size_t free_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (free_size < 24u * 1024u || largest < 4096u) {
    TPL_LOGE("mem",
             "S28.03 %s: internal low (free %u B, largest %u B; need 24 KB / "
             "4 KB)",
             point, (unsigned)free_size, (unsigned)largest);
    return false;
  }
  TPL_LOGI("mem", "S28.03 %s: internal OK (free %u B, largest %u B)", point,
           (unsigned)free_size, (unsigned)largest);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  /* Diagnostics first: everything after this point reaches the PSRAM ring,
   * the WebSocket sink and /log. */
  tpl_debug_log_setup();
  TPL_LOGI("boot", "28_hauszentrale starting");

  tpl_system_setup(0);  // no deep sleep
  haus_config_init();
  haus_can_bus_init();
  TPL_LOGI("boot", "boot #%lu, reset %s / %s",
           (unsigned long)tpl_config.bootCount,
           tpl_config.reset_reason_cpu0 ? tpl_config.reset_reason_cpu0 : "?",
           tpl_config.reset_reason_cpu1 ? tpl_config.reset_reason_cpu1 : "?");

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
   * but the panel still boots. The day file cannot be replayed until SNTP has
   * set the clock: before that `now_epoch` is 0, so `is_today` is false and
   * today's minute samples would be silently skipped. Restore after the WiFi
   * setup below, once time() is valid. */
  const bool history_ok = haus_history_init();
  if (!history_ok) TPL_LOGW("history", "history store unavailable");

  /* S9.03: ROM -> function table (seeded on first boot). */
  haus_sensor_roles_init();

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
      /* LVGL positions the cursor by its top-left corner. Centre the dot on
       * the actual touch point instead of drawing it down-right of the
       * finger. */
      lv_obj_set_style_translate_x(cursor, -12, 0);
      lv_obj_set_style_translate_y(cursor, -12, 0);
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
    internal_heap_ok("after ui_init");

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
  tpl_wifi_setup(true, false, (gpio_num_t)255);  // no status LED on this board
  /* Now that SNTP has run, today's day file can be replayed into L1 and the
   * recorder can start. Doing this before WiFi dropped the 1-day chart. */
  if (history_ok) {
    const size_t restored = haus_history_restore((uint32_t)time(nullptr));
    TPL_LOGI("history", "boot restore: %u columns", (unsigned)restored);
    haus_history_start_recorder();
  }
  TPL_LOGI("mem", "M1 after wifi: internal %u B, largest %u B",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  internal_heap_ok("after wifi");
  sntp_set_sync_interval(60UL * 60UL * 1000UL);  // refresh once an hour
  tpl_webserver_setup();
  TPL_LOGI("mem", "M2 after web: internal %u B, largest %u B",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  internal_heap_ok("after web");
  tpl_debug_log_register_endpoints();
  /* S23.14: /sd/status with the same figures the storage panel shows. */
  tpl_server.on("/sd/status", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    gui_storage_refresh();
    /* Handlers run serially on the loop task, whose stack is tight (the
     * Arduino core creates it with 8 KB). Keep the large structs off it:
     * with a CAN flood the call path otherwise overflows and reboots. */
    static HausState st;
    static haus_can_snapshot_t cs;
    gui_get_state(&st);
    haus_can_bus_get(&cs);
    const uint64_t total = st.can.sd_total_bytes;
    const uint64_t used = st.can.sd_used_bytes;
    const uint64_t free_bytes = total > used ? total - used : 0;
    static char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"initialized\":%s,\"type\":\"%s\",\"total\":%llu,"
             "\"used\":%llu,\"free\":%llu,\"floor\":%llu,"
             "\"headroom\":%llu,\"samples\":%llu,\"rollup\":%llu,"
             "\"logs\":%llu,\"last_rollover\":%lu,\"newest_file\":%lu,"
             "\"fault_since\":%lu,\"spiffs_total\":%lu,"
             "\"spiffs_used\":%lu,\"ring_bytes\":%lu,\"ring_hours\":%lu,"
             "\"ring_oldest\":%lu,\"ring_newest\":%lu,\"writing\":%s,"
             "\"can_running\":%s,\"mux_can\":%s,"
             "\"can_state\":%u,\"can_tec\":%lu,\"can_rec\":%lu,"
             "\"can_rx\":%lu,\"can_tx\":%lu,\"can_err\":%lu,"
             "\"can_fault\":%s,\"ui_hz\":%lu,\"ui_gap_ms\":%lu,"
             "\"reset\":%u,\"boot\":%lu}",
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
             (unsigned long)st.can.spiffs_total_bytes,
             (unsigned long)st.can.spiffs_used_bytes,
             (unsigned long)st.can.ring_bytes,
             (unsigned long)st.can.ring_hours,
             (unsigned long)st.can.ring_oldest_epoch,
             (unsigned long)st.can.ring_newest_epoch,
             st.can.sd_writing ? "true" : "false",
             haus_can_bus_running() ? "true" : "false",
             tpl_ws7_can_available() ? "true" : "false",
             (unsigned)cs.twai_state, (unsigned long)cs.tx_error_counter,
             (unsigned long)cs.rx_error_counter, (unsigned long)cs.rx_frames,
             (unsigned long)cs.tx_frames, (unsigned long)cs.bus_errors,
             cs.bus_fault ? "true" : "false", (unsigned long)s_ui_hz,
             (unsigned long)s_ui_gap_ms, (unsigned)tpl_config.reset_reason,
             (unsigned long)tpl_config.bootCount);
    tpl_server.send(200, "application/json", buf);
  });
  /* Explicit CAN intent for headless setup/tests (S23.03/S24.04). */
  tpl_server.on("/config/can", HTTP_GET, []() {
    const bool on = tpl_server.hasArg("on") && tpl_server.arg("on") == "1";
    gui_set_can_enabled(on);
    haus_config_set_can_enabled(on);
    tpl_server.send(200, "application/json",
                    on ? "{\"can\":true}" : "{\"can\":false}");
  });
  /* Light CAN-only telemetry: /sd/status refreshes the SD card, which must
   * not be hammered while the bus is under a flood (S17.07). */
  tpl_server.on("/can", HTTP_GET, []() {
    tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
    tpl_server.sendHeader("Connection", "close");
    static haus_can_snapshot_t cs;
    haus_can_bus_get(&cs);
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"running\":%s,\"state\":%u,\"tec\":%lu,\"rec\":%lu,"
             "\"rx\":%lu,\"tx\":%lu,\"err\":%lu,\"fault\":%s,"
             "\"ui_hz\":%lu,\"ui_gap_ms\":%lu,\"loop_stack\":%lu,"
             "\"heap\":%u}",
             haus_can_bus_running() ? "true" : "false", (unsigned)cs.twai_state,
             (unsigned long)cs.tx_error_counter,
             (unsigned long)cs.rx_error_counter, (unsigned long)cs.rx_frames,
             (unsigned long)cs.tx_frames, (unsigned long)cs.bus_errors,
             cs.bus_fault ? "true" : "false", (unsigned long)s_ui_hz,
             (unsigned long)s_ui_gap_ms,
             (unsigned long)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    tpl_server.send(200, "application/json", buf);
  });
  /* Diagnostic: what is actually in L1, and would the 1-day chart see it? */
  tpl_server.on("/history/stats", HTTP_GET, []() {
    static char buf[320];
    gui_sample_t* l1 = haus_history_l1();
    int total = 0;
    int climate = 0;
    int living = 0;
    uint32_t newest = 0;
    uint32_t newest_living = 0;
    if (l1) {
      for (size_t i = 0; i < HAUS_L1_SAMPLES; i++) {
        const gui_sample_t* s = &l1[i];
        if (s->epoch == 0) continue;
        total++;
        if (s->epoch > newest) newest = s->epoch;
        if (s->flags & SAMPLE_FLAG_CLIMATE_AHT20) climate++;
        if (s->living_c100 != HAUS_CAN_TEMP_EMPTY &&
            s->living_c100 != GUI_EMPTY) {
          living++;
          if (s->epoch > newest_living) newest_living = s->epoch;
        }
      }
    }
    snprintf(buf, sizeof(buf),
             "{\"l1_total\":%d,\"climate_flag\":%d,\"living_value\":%d,"
             "\"newest\":%lu,\"newest_living\":%lu,\"now\":%lu}",
             total, climate, living, (unsigned long)newest,
             (unsigned long)newest_living, (unsigned long)time(nullptr));
    tpl_server.send(200, "application/json", buf);
  });
  /* Guided GT911 calibration: five rounds of four cross points. */
  tpl_server.on("/touch/test", HTTP_GET, []() {
    haus_touch_test_request();
    tpl_server.send(200, "application/json", "{\"started\":true}");
  });
  tpl_server.on("/touch/stats", HTTP_GET, []() {
    const char* stats = haus_touch_test_last_stats();
    tpl_server.send(200, "application/json",
                    (stats && stats[0]) ? stats : "{\"n\":0}");
  });
  tpl_websocket_setup(NULL, NULL);

  /* S24.04: the persisted intent decides whether TWAI exists at all. With
   * CAN off, USB_SEL stays low and GPIO 19/20 remain the USB console. */
  if (haus_config_can_enabled()) {
    TPL_LOGI("settings", "CAN enabled in config; starting after 30 s");
    s_can_start_pending = true;
  } else {
    TPL_LOGI("settings", "CAN disabled in config; USB remains");
  }
  /* No net watchdog: WiFi is optional after boot. The WiFi manager keeps
   * retrying the link, while the panel stays fully operational offline. */
  tpl_command_setup(NULL);

  /* Boot-time flash access can drop PSRAM framebuffer writes (cache off),
   * leaving missing pixels in once-drawn art such as the cabinet outline.
   * Repaint the whole screen once after setup has finished. */
  gui_request_redraw();
}

/* ─── Loop ───────────────────────────────────────────────────────────────── */

void loop() {
  haus_touch_test_poll();
  static uint32_t last_ui_ms = 0;
  static uint32_t last_loop_us = 0;
  static uint32_t loops = 0;
  static uint32_t gap_max_ms = 0;
  static uint32_t window_ms = 0;
  static uint32_t last_anim_ms = 0;
  static bool s_ota_backlight_off = false;
  const uint32_t now = millis();
  /* S24.04: start TWAI from the loop, not setup(). Doing it inside setup()
   * while WiFi/OTA tasks are still coming up leaves the panel unresponsive;
   * the same call 2 s later is known-good from bench bring-up. */
  if (s_can_start_pending && now >= 30000u) {
    s_can_start_pending = false;
    /* Re-read the intent: the user may have switched CAN off in Settings
     * during the boot window. */
    if (haus_config_can_enabled() && !haus_can_bus_running())
      gui_set_can_enabled(true);
  }
  const uint32_t loop_us = micros();
  const uint32_t gap_ms = last_loop_us ? (loop_us - last_loop_us) / 1000u : 0u;
  last_loop_us = loop_us;
  if (gap_ms > gap_max_ms) gap_max_ms = gap_ms;
  loops++;
  if (now - window_ms >= 1000u) {
    window_ms = now;
    s_ui_gap_ms = gap_max_ms;
    s_ui_hz = loops;
    loops = 0;
    gap_max_ms = 0;
  }

  /* OTA writes flash with the cache/PSRAM disabled while the RGB DMA is still
   * scanning the PSRAM framebuffer, which garbles the panel completely. Blank
   * the backlight for the duration; on a failed update restore it. */
  if (tpl_config.ota_ongoing != s_ota_backlight_off) {
    s_ota_backlight_off = tpl_config.ota_ongoing;
    tpl_ws7_backlight(!s_ota_backlight_off);
    TPL_LOGI("ota", "backlight %s for OTA", s_ota_backlight_off ? "OFF" : "ON");
  }

  /* Data refresh at 1 Hz. Refreshing at loop rate invalidates every label
   * hundreds of times per second, which the single-buffered RGB panel shows
   * as flicker and tearing. */
  if (now - last_ui_ms >= 1000) {
    last_ui_ms = now;
    gui_update_state();
    if (s_disp) haus_ui_update();
  }

  /* Flame breathing at 20 Hz; it only touches opacity when it changes. */
  if (now - last_anim_ms >= 50) {
    last_anim_ms = now;
    haus_ui_animate();
  }

  if (!haus_touch_test_active() && tpl_ws7_touch_points() >= 3) {
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

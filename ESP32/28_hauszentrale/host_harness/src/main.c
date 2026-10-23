/**
 * Headless LVGL PNG renderer for 28_hauszentrale.
 *
 * Renders the PRODUCTIVE screens (src/ui/haus_ui.c — the same file the ESP32
 * firmware builds) to 800x480 PNGs. There is no harness-local UI code: what
 * this renders is what the device runs.
 *
 * Usage: ./hauszentrale_host [output_dir]
 * Default output: output_<screen>.png for every screen and home variant.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump_data.h"
#include "haus_gui.h"
#include "haus_ui.h"
#include "lodepng.h"
#include "lvgl.h"

#define SCREEN_W 800
#define SCREEN_H 480
#define BUF_SIZE (SCREEN_W * SCREEN_H * 4)

/* ─── PNG rendering ─────────────────────────────────────────────────────────
 */

static uint32_t lvgl_to_lodepng(uint32_t argb) {
  return ((argb & 0x000000FF) << 16) | ((argb & 0x0000FF00)) |
         ((argb & 0x00FF0000) >> 16) | ((argb & 0xFF000000));
}

/* Rendering goes straight into our own buffer, so flashing is always "ready".
 */
static void flush_cb(lv_display_t* disp, const lv_area_t* area,
                     uint8_t* px_map) {
  (void)area;
  (void)px_map;
  lv_display_flush_ready(disp);
}

static int render_to_png(const char* filename) {
  lv_display_t* disp = lv_display_get_default();
  lv_draw_buf_t* buf = lv_display_get_buf_active(disp);
  if (!buf) {
    fprintf(stderr, "error: no active buffer\n");
    return 1;
  }

  size_t rgba_size = (size_t)SCREEN_W * SCREEN_H * 4;
  uint8_t* rgba = malloc(rgba_size);
  if (!rgba) {
    fprintf(stderr, "error: malloc rgba failed\n");
    return 1;
  }

  uint32_t* src_pixels = (uint32_t*)buf->data;
  for (uint32_t i = 0; i < (size_t)SCREEN_W * SCREEN_H; i++) {
    ((uint32_t*)rgba)[i] = lvgl_to_lodepng(src_pixels[i]);
  }

  unsigned char* png_data = NULL;
  size_t png_size = 0;
  unsigned err =
      lodepng_encode32(&png_data, &png_size, rgba, SCREEN_W, SCREEN_H);
  free(rgba);

  if (err) {
    fprintf(stderr, "lodepng error %u: %s\n", err, lodepng_error_text(err));
    return 1;
  }

  FILE* fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "error: could not open file for writing: %s\n", filename);
    free(png_data);
    return 1;
  }

  fwrite(png_data, 1, png_size, fp);
  fclose(fp);
  free(png_data);

  printf("saved %s (%dx%d)\n", filename, SCREEN_W, SCREEN_H);
  return 0;
}

/* ─── Screen wrappers ────────────────────────────────────────────────────── */

void mock_set_home_case(int target_c100, int pwm_permille, bool fresh);

enum screen_kind {
  SK_HOME,
  SK_HEATER,
  SK_POWER,
  SK_LIVING,
  SK_PUMP_ANGLE,
  SK_PUMP_RMS,
  SK_SETTINGS,
  SK_STORAGE,
  SK_CAN,
  SK_LOG,
  SK_ROLES,
  SK_SML,
  SK_HEATER_CFG,
  SK_HOME_OFF,
  SK_HOME_40,
  SK_HOME_60,
};

typedef struct {
  enum screen_kind kind;
  int range; /* 0 day, 1 week, 2 month */
  const char* name;
} screen_def_t;

/* ─── Main ──────────────────────────────────────────────────────────────────
 */

int main(int argc, char* argv[]) {
  const char* output_dir = ".";
  const char* dump_dir = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
      dump_dir = argv[++i];
    } else if (strncmp(argv[i], "--dump=", 7) == 0) {
      dump_dir = argv[i] + 7;
    } else {
      output_dir = argv[i];
    }
  }

  if (dump_load(dump_dir)) {
    printf("dump: %s (samples and hourly columns loaded)\n",
           dump_directory());
  } else {
    fprintf(stderr,
            "warning: no dump found; using the synthetic heater model\n");
  }

  lv_init();

  uint8_t* raw_buf = malloc(BUF_SIZE);
  if (!raw_buf) {
    fprintf(stderr, "error: malloc failed\n");
    return 1;
  }

  lv_draw_buf_t draw_buf;
  lv_result_t res =
      lv_draw_buf_init(&draw_buf, SCREEN_W, SCREEN_H, LV_COLOR_FORMAT_ARGB8888,
                       LV_STRIDE_AUTO, raw_buf, BUF_SIZE);
  if (res != LV_RESULT_OK) {
    fprintf(stderr, "error: lv_draw_buf_init failed\n");
    free(raw_buf);
    return 1;
  }
  draw_buf.unaligned_data = raw_buf;

  lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0); /* landscape */
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_draw_buffers(disp, &draw_buf, NULL);
  lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);

  static const screen_def_t screens[] = {
      {SK_HOME, 0, "home"},
      {SK_HEATER, 0, "heater_day"},
      {SK_HEATER, 1, "heater_week"},
      {SK_HEATER, 2, "heater_month"},
      {SK_POWER, 0, "power_day"},
      {SK_POWER, 1, "power_week"},
      {SK_POWER, 2, "power_month"},
      {SK_LIVING, 0, "living_day"},
      {SK_LIVING, 1, "living_week"},
      {SK_LIVING, 2, "living_month"},
      {SK_PUMP_ANGLE, 0, "pump_angle_day"},
      {SK_PUMP_ANGLE, 1, "pump_angle_week"},
      {SK_PUMP_ANGLE, 2, "pump_angle_month"},
      {SK_PUMP_RMS, 0, "pump_rms_day"},
      {SK_PUMP_RMS, 1, "pump_rms_week"},
      {SK_PUMP_RMS, 2, "pump_rms_month"},
      {SK_SETTINGS, 0, "settings"},
      {SK_STORAGE, 0, "storage"},
      {SK_CAN, 0, "can"},
      {SK_LOG, 0, "log"},
      {SK_ROLES, 0, "roles"},
      {SK_SML, 0, "sml"},
      {SK_HEATER_CFG, 0, "heater_config"},
      {SK_HOME_OFF, 0, "home_off"},
      {SK_HOME_40, 0, "home_40"},
      {SK_HOME_60, 0, "home_60"},
  };

  const char* only = getenv("HAUS_ONLY");
  for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
    const screen_def_t* sd = &screens[i];
    if (only && strcmp(only, sd->name) != 0) continue;

    char filename[512];
    snprintf(filename, sizeof(filename), "%s/output_%s.png", output_dir,
             sd->name);

    haus_ui_deinit();
    haus_ui_set_range(0);
    /* LVGL only repaints dirty areas into the shared display buffer. The
     * previous screen's pixels would otherwise survive where the new screen
     * has no opaque object, which showed up as uninitialised noise around
     * the chart. Start every screen from the panel background. */
    {
      uint32_t* px = (uint32_t*)raw_buf;
      for (size_t k = 0; k < BUF_SIZE / 4; k++) px[k] = 0xFF0E0C0Bu;
    }

    switch (sd->kind) {
      case SK_HOME:
        haus_ui_init();
        break;
      case SK_HEATER:
        haus_ui_build_detail();
        haus_ui_set_range(sd->range);
        break;
      case SK_POWER:
        haus_ui_build_power();
        haus_ui_set_range(sd->range);
        break;
      case SK_LIVING:
        haus_ui_build_living();
        haus_ui_set_range(sd->range);
        break;
      case SK_PUMP_ANGLE:
        haus_ui_build_ac();
        haus_ui_set_ac_series(1);
        haus_ui_set_range(sd->range);
        break;
      case SK_PUMP_RMS:
        haus_ui_build_ac();
        haus_ui_set_ac_series(0);
        haus_ui_set_range(sd->range);
        break;
      case SK_SETTINGS:
        haus_ui_build_settings();
        break;
      case SK_STORAGE:
        haus_ui_build_storage();
        break;
      case SK_CAN:
        haus_ui_build_can();
        break;
      case SK_LOG:
        haus_ui_build_log();
        break;
      case SK_ROLES:
        haus_ui_build_roles();
        break;
      case SK_SML:
        haus_ui_build_sml();
        break;
      case SK_HEATER_CFG:
        haus_ui_build_heater_cfg();
        break;
      case SK_HOME_OFF:
        mock_set_home_case(6000, 1000, false);
        haus_ui_init();
        break;
      case SK_HOME_40:
        mock_set_home_case(4000, 300, true);
        haus_ui_init();
        break;
      case SK_HOME_60:
        mock_set_home_case(6000, 200, true);
        haus_ui_init();
        break;
    }

    haus_ui_update();
    /* Force a full-screen repaint: LVGL otherwise only refreshes invalid
     * objects and can leave parts of the previous screen (or uninitialised
     * pixels) in the shared display buffer. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    render_to_png(filename);
  }

  free(raw_buf);
  lv_deinit();
  return 0;
}

/**
 * Headless LVGL PNG renderer for 28_hauszentrale.
 *
 * Renders the PRODUCTIVE screens (src/ui/haus_ui.c — the same file the ESP32
 * firmware builds) to 480x800 PNGs. There is no harness-local UI code: what
 * this renders is what the device runs.
 *
 * Usage: ./hauszentrale_host [output_dir]
 * Default output: output_home.png, output_chart.png, output_detail.png
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "lodepng.h"
#include "haus_gui.h"
#include "haus_ui.h"

#define SCREEN_W 480
#define SCREEN_H 800
#define BUF_SIZE (SCREEN_W * SCREEN_H * 4)

/* ─── PNG rendering ───────────────────────────────────────────────────────── */

static uint32_t lvgl_to_lodepng(uint32_t argb) {
    return ((argb & 0x000000FF) << 16) |
           ((argb & 0x0000FF00)      ) |
           ((argb & 0x00FF0000) >> 16) |
           ((argb & 0xFF000000)     );
}

/* Rendering goes straight into our own buffer, so flashing is always "ready". */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static int render_to_png(const char *filename) {
    lv_display_t *disp = lv_display_get_default();
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (!buf) {
        fprintf(stderr, "error: no active buffer\n");
        return 1;
    }

    size_t rgba_size = (size_t)SCREEN_W * SCREEN_H * 4;
    uint8_t *rgba = malloc(rgba_size);
    if (!rgba) {
        fprintf(stderr, "error: malloc rgba failed\n");
        return 1;
    }

    uint32_t *src_pixels = (uint32_t *)buf->data;
    for (uint32_t i = 0; i < (size_t)SCREEN_W * SCREEN_H; i++) {
        ((uint32_t *)rgba)[i] = lvgl_to_lodepng(src_pixels[i]);
    }

    unsigned char *png_data = NULL;
    size_t png_size = 0;
    unsigned err = lodepng_encode32(&png_data, &png_size, rgba, SCREEN_W, SCREEN_H);
    free(rgba);

    if (err) {
        fprintf(stderr, "lodepng error %u: %s\n", err, lodepng_error_text(err));
        return 1;
    }

    FILE *fp = fopen(filename, "wb");
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

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *output_dir = ".";
    if (argc > 1) {
        output_dir = argv[1];
    }

    lv_init();

    uint8_t *raw_buf = malloc(BUF_SIZE);
    if (!raw_buf) {
        fprintf(stderr, "error: malloc failed\n");
        return 1;
    }

    lv_draw_buf_t draw_buf;
    lv_result_t res = lv_draw_buf_init(&draw_buf, SCREEN_W, SCREEN_H,
                                        LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO,
                                        raw_buf, BUF_SIZE);
    if (res != LV_RESULT_OK) {
        fprintf(stderr, "error: lv_draw_buf_init failed\n");
        free(raw_buf);
        return 1;
    }
    draw_buf.unaligned_data = raw_buf;

    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_draw_buffers(disp, &draw_buf, NULL);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);

    const char *screen_names[] = {"home", "chart", "detail"};
    void (*build_funcs[])(void) = {
        (void (*)(void))haus_ui_init,
        (void (*)(void))haus_ui_build_chart,
        (void (*)(void))haus_ui_build_detail,
    };

    for (int i = 0; i < 3; i++) {
        char filename[512];
        snprintf(filename, sizeof(filename), "%s/output_%s.png", output_dir, screen_names[i]);

        /* Clear screen */
        lv_obj_t *root = lv_screen_active();
        while (lv_obj_get_child_cnt(root) > 0) {
            lv_obj_delete(lv_obj_get_child(root, 0));
        }

        /* Build productive screen */
        build_funcs[i]();

        /* Apply live state (home reads gui_get_state via haus_ui_update) */
        if (i == 0) {
            haus_ui_update();
        }

        lv_refr_now(disp);
        render_to_png(filename);
    }

    free(raw_buf);
    lv_deinit();
    return 0;
}

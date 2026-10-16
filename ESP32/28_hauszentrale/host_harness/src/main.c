/**
 * Headless LVGL PNG renderer for 28_hauszentrale.
 * Renders all 3 screens (home, chart, detail) to separate PNG files.
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

/* ─── PNG rendering (from commit 63bbcad) ─────────────────────────────────── */

static uint32_t lvgl_to_lodepng(uint32_t argb) {
    return ((argb & 0x000000FF) << 16) |
           ((argb & 0x0000FF00)      ) |
           ((argb & 0x00FF0000) >> 16) |
           ((argb & 0xFF000000)     );
}

/* Rendering goes straight into our own buffer, so flushing is always "ready".
 * Without this, disp->flushing stays set and the next lv_refr_now() spins
 * forever in wait_for_flushing() (single-buffered mode). */
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

/* ─── Chart screen (S12) ──────────────────────────────────────────────────── */

static void build_chart_screen(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x1a1a2e), 0);

    /* Header: Back button + title */
    lv_obj_t *back = lv_label_create(root);
    lv_obj_set_pos(back, 16, 16);
    lv_obj_set_style_text_font(back, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0xffffff), 0);
    lv_label_set_text(back, LV_SYMBOL_LEFT);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Chamber");

    /* Range selector */
    lv_obj_t *range_sel = lv_label_create(root);
    lv_obj_set_pos(range_sel, SCREEN_W - 100, 16);
    lv_obj_set_style_text_font(range_sel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(range_sel, lv_color_hex(0xe94560), 0);
    lv_label_set_text(range_sel, "1d 1w 1m");

    /* Chart canvas (440×260) */
    lv_obj_t *canvas = lv_canvas_create(root);
    lv_obj_set_size(canvas, 440, 260);
    lv_obj_set_pos(canvas, 20, 80);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(canvas, 8, 0);
    lv_obj_set_style_border_width(canvas, 1, 0);
    lv_obj_set_style_border_color(canvas, lv_color_hex(0x444466), 0);

    /* Axes */
    static lv_point_precise_t y_pts[2] = {{10, 10}, {10, 250}};
    lv_obj_t *y_axis = lv_line_create(canvas);
    lv_line_set_points(y_axis, y_pts, 2);
    lv_obj_set_style_line_color(y_axis, lv_color_hex(0x444466), 0);
    lv_obj_set_style_line_width(y_axis, 1, 0);

    static lv_point_precise_t x_pts[2] = {{10, 250}, {430, 250}};
    lv_obj_t *x_axis = lv_line_create(canvas);
    lv_line_set_points(x_axis, x_pts, 2);
    lv_obj_set_style_line_color(x_axis, lv_color_hex(0x444466), 0);
    lv_obj_set_style_line_width(x_axis, 1, 0);

    /* Y axis labels */
    lv_obj_t *yl1 = lv_label_create(canvas);
    lv_obj_set_pos(yl1, 2, 10);
    lv_obj_set_style_text_font(yl1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yl1, lv_color_hex(0x888888), 0);
    lv_label_set_text(yl1, "max");

    lv_obj_t *yl2 = lv_label_create(canvas);
    lv_obj_set_pos(yl2, 2, 130);
    lv_obj_set_style_text_font(yl2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yl2, lv_color_hex(0x888888), 0);
    lv_label_set_text(yl2, "0");

    lv_obj_t *yl3 = lv_label_create(canvas);
    lv_obj_set_pos(yl3, 2, 240);
    lv_obj_set_style_text_font(yl3, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(yl3, lv_color_hex(0x888888), 0);
    lv_label_set_text(yl3, "min");

    /* X axis labels */
    lv_obj_t *xl1 = lv_label_create(canvas);
    lv_obj_set_pos(xl1, 10, 255);
    lv_obj_set_style_text_font(xl1, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(xl1, lv_color_hex(0x888888), 0);
    lv_label_set_text(xl1, "now");

    lv_obj_t *xl2 = lv_label_create(canvas);
    lv_obj_set_pos(xl2, 400, 255);
    lv_obj_set_style_text_font(xl2, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(xl2, lv_color_hex(0x888888), 0);
    lv_label_set_text(xl2, "ago");

    /* Draw chart data line */
    gui_sample_t samples[440];
    uint16_t count = 0;
    const gui_haus_state_t *state = gui_get_state();
    uint32_t now = state->time.timestamp;
    uint32_t start = now - 86400;  /* 1 day */

    gui_history_request_t hist_req = {
        .start_epoch = start,
        .end_epoch = now,
        .samples = samples,
        .max_samples = 440,
        .out_count = &count,
    };

    gui_get_history(&hist_req);

    if (count > 1 && count <= 440) {
        static lv_point_precise_t data_pts[440];
        for (uint16_t i = 0; i < count; i++) {
            double x = 10.0 + ((double)i / (count - 1)) * 420.0;
            int16_t temp = samples[i].temp[0];  /* chamber */

            double y;
            if (temp == GUI_EMPTY) {
                y = 250.0;
            } else {
                double c = temp / 100.0;
                y = 250.0 - ((c - 15.0) / 65.0) * 240.0;
                if (y < 10.0) y = 10.0;
                if (y > 250.0) y = 250.0;
            }

            data_pts[i].x = (lv_value_precise_t)x;
            data_pts[i].y = (lv_value_precise_t)y;
        }

        lv_obj_t *line = lv_line_create(canvas);
        lv_line_set_points(line, data_pts, count);
        lv_obj_set_style_line_color(line, lv_color_hex(0xe94560), 0);
        lv_obj_set_style_line_width(line, 2, 0);
    }

    /* Gradient fill at bottom */
    lv_obj_t *fill = lv_obj_create(canvas);
    lv_obj_set_size(fill, 440, 30);
    lv_obj_set_pos(fill, 0, 230);
    lv_obj_set_style_bg_color(fill, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_bg_opa(fill, 30, 0);
    lv_obj_set_style_radius(fill, 0, 0);
    lv_obj_set_style_border_width(fill, 0, 0);

    /* Readout */
    lv_obj_t *readout = lv_label_create(root);
    lv_obj_set_pos(readout, 20, 360);
    lv_obj_set_style_text_font(readout, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(readout, lv_color_hex(0xffffff), 0);
    char buf[256];
    if (count > 0) {
        int16_t min_t = GUI_EMPTY, max_t = GUI_EMPTY, last_t = GUI_EMPTY;
        double sum = 0.0;
        int valid_count = 0;

        for (uint16_t i = 0; i < count; i++) {
            int16_t temp = samples[i].temp[0];
            if (temp != GUI_EMPTY) {
                if (min_t == GUI_EMPTY || temp < min_t) min_t = temp;
                if (max_t == GUI_EMPTY || temp > max_t) max_t = temp;
                last_t = temp;
                sum += temp / 100.0;
                valid_count++;
            }
        }

        if (valid_count > 0) {
            double mean = sum / valid_count;
            snprintf(buf, sizeof(buf),
                     "min: %4.1f°C  mean: %4.1f°C  max: %4.1f°C  now: %4.1f°C\n"
                     "Resolution: minute  Samples: %u",
                     (min_t == GUI_EMPTY) ? 0.0 : min_t / 100.0,
                     mean,
                     (max_t == GUI_EMPTY) ? 0.0 : max_t / 100.0,
                     (last_t == GUI_EMPTY) ? 0.0 : last_t / 100.0,
                     count);
        } else {
            snprintf(buf, sizeof(buf), "no data  Resolution: minute");
        }
    } else {
        snprintf(buf, sizeof(buf), "no data  Resolution: minute");
    }
    lv_label_set_text(readout, buf);
}

/* ─── Detail screen (S13) ─────────────────────────────────────────────────── */

static void build_detail_screen(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x1a1a2e), 0);

    /* Header */
    lv_obj_t *back = lv_label_create(root);
    lv_obj_set_pos(back, 16, 16);
    lv_obj_set_style_text_font(back, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(back, lv_color_hex(0xffffff), 0);
    lv_label_set_text(back, LV_SYMBOL_LEFT);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Chamber Detail");

    /* Target line label */
    lv_obj_t *target = lv_label_create(root);
    lv_obj_set_pos(target, 20, 60);
    lv_obj_set_style_text_font(target, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target, lv_color_hex(0xe94560), 0);
    lv_label_set_text(target, "Target: 60.0°C (dashed)");

    lv_obj_t *tat = lv_label_create(root);
    lv_obj_set_pos(tat, 20, 85);
    lv_obj_set_style_text_font(tat, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(tat, lv_color_hex(0x888888), 0);
    lv_label_set_text(tat, "Time above target: -- (last 24h)");

    /* Chart (reuse chart canvas logic) */
    lv_obj_t *canvas = lv_canvas_create(root);
    lv_obj_set_size(canvas, 440, 260);
    lv_obj_set_pos(canvas, 20, 110);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(canvas, 8, 0);
    lv_obj_set_style_border_width(canvas, 1, 0);
    lv_obj_set_style_border_color(canvas, lv_color_hex(0x444466), 0);

    /* Dashed target line */
    static lv_point_precise_t target_pts[2] = {{20, 100}, {420, 100}};
    lv_obj_t *target_line = lv_line_create(canvas);
    lv_line_set_points(target_line, target_pts, 2);
    lv_obj_set_style_line_color(target_line, lv_color_hex(0x888888), 0);
    lv_obj_set_style_line_width(target_line, 1, 0);
    lv_obj_set_style_line_dash_width(target_line, 6, 0);
    lv_obj_set_style_line_dash_gap(target_line, 6, 0);

    /* Actual data line (solid) */
    static lv_point_precise_t data_pts[3] = {{20, 180}, {220, 120}, {420, 150}};
    lv_obj_t *data_line = lv_line_create(canvas);
    lv_line_set_points(data_line, data_pts, 3);
    lv_obj_set_style_line_color(data_line, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_line_width(data_line, 2, 0);

    /* Readouts */
    lv_obj_t *readouts = lv_label_create(root);
    lv_obj_set_pos(readouts, 20, 390);
    lv_obj_set_style_text_font(readouts, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(readouts, lv_color_hex(0xffffff), 0);
    lv_label_set_text(readouts,
                      "min: --°C  mean: --°C  max: --°C  now: --°C");

    /* Data age */
    lv_obj_t *age = lv_label_create(root);
    lv_obj_set_pos(age, 20, 415);
    lv_obj_set_style_text_font(age, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(age, lv_color_hex(0x888888), 0);
    lv_label_set_text(age, "Data age: --");

    /* Range selector */
    lv_obj_t *range_sel = lv_label_create(root);
    lv_obj_set_pos(range_sel, SCREEN_W - 100, 16);
    lv_obj_set_style_text_font(range_sel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(range_sel, lv_color_hex(0xe94560), 0);
    lv_label_set_text(range_sel, "1d 1w 1m");

    /* Footer: sensor sub-line (Detail 5) */
    lv_obj_t *sensors = lv_label_create(root);
    lv_obj_set_pos(sensors, 20, SCREEN_H - 100);
    lv_obj_set_style_text_font(sensors, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sensors, lv_color_hex(0x888888), 0);
    lv_label_set_text(sensors, "AHT20 - BMP280 - H1");
}

/* ─── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *output_dir = ".";
    if (argc > 1) {
        output_dir = argv[1];
    }

    /* Initialize LVGL */
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

    /* Render all 3 screens to separate PNGs */
    const char *screen_names[] = {"home", "chart", "detail"};
    void (*build_funcs[]) (void) = {
        (void (*)(void))haus_ui_init,
        build_chart_screen,
        build_detail_screen,
    };

    for (int i = 0; i < 3; i++) {
        char filename[512];
        snprintf(filename, sizeof(filename), "%s/output_%s.png", output_dir, screen_names[i]);

        /* Clear screen */
        lv_obj_t *root = lv_screen_active();
        while (lv_obj_get_child_cnt(root) > 0) {
            lv_obj_delete(lv_obj_get_child(root, 0));
        }

        /* Build screen */
        build_funcs[i]();

        /* Apply live state (home screen reads gui_get_state via haus_ui_update) */
        if (i == 0) {
            haus_ui_update();
        }

        /* Render and save */
        lv_refr_now(disp);
        render_to_png(filename);
    }

    /* Cleanup */
    free(raw_buf);
    lv_deinit();
    return 0;
}

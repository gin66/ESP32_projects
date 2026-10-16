/**
 * Headless LVGL PNG renderer for 28_hauszentrale.
 * Renders the UI to a 480×800 32-bit buffer and saves as PNG.
 * Usage: ./hauszentrale_host <output.png>
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "lodepng.h"

#define SCREEN_W 480
#define SCREEN_H 800
#define BUF_SIZE (SCREEN_W * SCREEN_H * 4)  /* 32-bit = 4 bytes/pixel */

/* Reverse all 4 bytes: 0xAARRGGBB -> 0xRRGGBBAA (lodepng expects RGBA little-endian) */
static uint32_t lvgl_to_lodepng(uint32_t argb) {
    return ((argb & 0x000000FF) << 16) |
           ((argb & 0x0000FF00)      ) |
           ((argb & 0x00FF0000) >> 16) |
           ((argb & 0xFF000000)     );
}

/* Render framebuffer to PNG */
static int render_to_png(const char * filename) {
    lv_display_t * disp = lv_display_get_default();
    lv_draw_buf_t * buf = lv_display_get_buf_active(disp);
    if (!buf) {
        fprintf(stderr, "error: no active buffer\n");
        return 1;
    }

    /* Allocate RGBA output buffer */
    size_t rgba_size = (size_t)SCREEN_W * SCREEN_H * 4;
    uint8_t * rgba = malloc(rgba_size);
    if (!rgba) {
        fprintf(stderr, "error: malloc rgba failed\n");
        return 1;
    }

    /* Convert and copy 32-bit pixels (reverse byte order) */
    uint32_t * src_pixels = (uint32_t *)buf->data;
    for (uint32_t i = 0; i < (size_t)SCREEN_W * SCREEN_H; i++) {
        ((uint32_t *)rgba)[i] = lvgl_to_lodepng(src_pixels[i]);
    }

    /* Debug: print first 5 pixels (converted) */
    printf("debug: first 5 pixels (after byte swap):\n");
    for (int i = 0; i < 5; i++) {
        uint32_t src = src_pixels[i];
        uint32_t dst = ((uint32_t *)rgba)[i];
        uint8_t r = (dst >> 16) & 0xFF;
        uint8_t g = (dst >> 8) & 0xFF;
        uint8_t b = dst & 0xFF;
        uint8_t a = (dst >> 24) & 0xFF;
        printf("  [%d] ARGB=0x%08X -> RGBA=0x%08X (R=%02X G=%02X B=%02X A=%02X)\n", 
               i, src, dst, r, g, b, a);
    }

    /* Encode as PNG (32-bit RGBA) */
    unsigned char * png_data = NULL;
    size_t png_size = 0;
    unsigned err = lodepng_encode32(&png_data, &png_size, rgba, SCREEN_W, SCREEN_H);
    free(rgba);

    if (err) {
        fprintf(stderr, "lodepng error %u: %s\n", err, lodepng_error_text(err));
        return 1;
    }

    /* Write PNG data to file */
    FILE * fp = fopen(filename, "wb");
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

/* Build a sample UI with pure primary colors for verification */
static void build_ui(void) {
    lv_obj_t * root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);  /* black background */

    /* Red rectangle (top-left) */
    lv_obj_t * red = lv_obj_create(root);
    lv_obj_set_size(red, 200, 150);
    lv_obj_set_style_bg_color(red, lv_color_hex(0xFF0000), 0);  /* pure red */
    lv_obj_align(red, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_t * red_label = lv_label_create(red);
    lv_label_set_text(red_label, "RED");
    lv_obj_set_style_text_color(red_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(red_label);

    /* Green rectangle (top-right) */
    lv_obj_t * green = lv_obj_create(root);
    lv_obj_set_size(green, 200, 150);
    lv_obj_set_style_bg_color(green, lv_color_hex(0x00FF00), 0);  /* pure green */
    lv_obj_align(green, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_t * green_label = lv_label_create(green);
    lv_label_set_text(green_label, "GREEN");
    lv_obj_set_style_text_color(green_label, lv_color_hex(0x000000), 0);
    lv_obj_center(green_label);

    /* Blue rectangle (bottom) */
    lv_obj_t * blue = lv_obj_create(root);
    lv_obj_set_size(blue, 440, 150);
    lv_obj_set_style_bg_color(blue, lv_color_hex(0x0000FF), 0);  /* pure blue */
    lv_obj_align(blue, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t * blue_label = lv_label_create(blue);
    lv_label_set_text(blue_label, "BLUE");
    lv_obj_set_style_text_color(blue_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(blue_label);

    /* White text (center) */
    lv_obj_t * white = lv_label_create(root);
    lv_label_set_text(white, "WHITE");
    lv_obj_set_style_text_font(white, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(white, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(white, LV_ALIGN_CENTER, 0, 0);
}

int main(int argc, char *argv[]) {
    const char * filename = "output.png";
    if (argc > 1) filename = argv[1];

    /* Initialize LVGL */
    lv_init();

    /* Allocate a raw framebuffer (32-bit, 4 bytes/pixel) */
    uint8_t * raw_buf = malloc(BUF_SIZE);
    if (!raw_buf) {
        fprintf(stderr, "error: malloc failed\n");
        return 1;
    }

    /* Initialize a draw buffer with 32-bit color */
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

    /* Create the display */
    lv_display_t * disp = lv_display_create(SCREEN_W, SCREEN_H);

    /* Set our draw buffer */
    lv_display_set_draw_buffers(disp, &draw_buf, NULL);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_FULL);

    /* Build the UI */
    build_ui();

    /* Force a full render to the buffer */
    lv_refr_now(disp);

    /* Save as PNG */
    int rc = render_to_png(filename);

    /* Cleanup */
    free(raw_buf);
    lv_deinit();
    return rc;
}

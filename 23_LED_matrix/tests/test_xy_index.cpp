#include "unity.h"
#include "leds.h"
#include <cmath>
#include "../include/led_effects.h"

#define WIDTH 32
#define HEIGHT 32

void test_all_pixels(void) {
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint16_t expected = leds[y][x];
            uint16_t actual = xyToIndex(x, y, WIDTH, HEIGHT);
            if (expected != actual) {
                printf("FAIL at x=%d y=%d: expected %u, got %u\n", x, y, expected, actual);
                _unity_test_failed = 1;
                return;
            }
        }
    }
}

void test_estimate_current_all_off_1_panel(void) {
    // all off with 32x8: 322mA = 322000uA
    LedColor pixels[256];
    for (int i = 0; i < 256; i++) {
        pixels[i] = LedColor(0, 0, 0);
    }
    uint32_t current = estimateCurrent(pixels, 256, 1, 255);
    if (current < 317000 || current > 327000) {
        printf("FAIL: expected ~322000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void test_estimate_current_all_red_1_panel(void) {
    // 100% Brightness red=255 for 32x8 LEDs: 3.37A = 3370000uA
    LedColor pixels[256];
    for (int i = 0; i < 256; i++) {
        pixels[i] = LedColor(255, 0, 0);
    }
    uint32_t current = estimateCurrent(pixels, 256, 1, 255);
    if (current < 3320000 || current > 3420000) {
        printf("FAIL: expected ~3370000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void test_estimate_current_all_green_1_panel(void) {
    // 100% Brightness green=255 for 32x8 LEDs: 3.19A = 3190000uA
    LedColor pixels[256];
    for (int i = 0; i < 256; i++) {
        pixels[i] = LedColor(0, 255, 0);
    }
    uint32_t current = estimateCurrent(pixels, 256, 1, 255);
    if (current < 3140000 || current > 3240000) {
        printf("FAIL: expected ~3190000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void test_estimate_current_all_blue_1_panel(void) {
    // 100% Brightness blue=255 for 32x8 LEDs: 3.34A = 3340000uA
    LedColor pixels[256];
    for (int i = 0; i < 256; i++) {
        pixels[i] = LedColor(0, 0, 255);
    }
    uint32_t current = estimateCurrent(pixels, 256, 1, 255);
    if (current < 3290000 || current > 3390000) {
        printf("FAIL: expected ~3340000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void test_estimate_current_all_off_3_panels(void) {
    // all off with 32x24: 580mA = 580000uA
    LedColor pixels[768];
    for (int i = 0; i < 768; i++) {
        pixels[i] = LedColor(0, 0, 0);
    }
    uint32_t current = estimateCurrent(pixels, 768, 3, 255);
    if (current < 570000 || current > 590000) {
        printf("FAIL: expected ~580000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void test_estimate_current_10percent_white_3_panels(void) {
    // 10% Brightness, red/blue/green=255, 32x24 => 3.4A = 3400000uA
    LedColor pixels[768];
    for (int i = 0; i < 768; i++) {
        pixels[i] = LedColor(25, 25, 25);  // ~10% of 255
    }
    uint32_t current = estimateCurrent(pixels, 768, 3, 255);
    if (current < 3200000 || current > 3600000) {
        printf("FAIL: expected ~3400000uA, got %uuA\n", current);
        _unity_test_failed = 1;
        return;
    }
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_all_pixels);
    RUN_TEST(test_estimate_current_all_off_1_panel);
    RUN_TEST(test_estimate_current_all_red_1_panel);
    RUN_TEST(test_estimate_current_all_green_1_panel);
    RUN_TEST(test_estimate_current_all_blue_1_panel);
    RUN_TEST(test_estimate_current_all_off_3_panels);
    RUN_TEST(test_estimate_current_10percent_white_3_panels);
    return UNITY_END();
}

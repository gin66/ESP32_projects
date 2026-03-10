#include "unity.h"
#include "leds.h"
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

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_all_pixels);
    return UNITY_END();
}

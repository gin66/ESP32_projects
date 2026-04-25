#include "led_effects.h"

void calculateWheel(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    int16_t cx = width / 2;
    int16_t cy = height / 2;
    int16_t maxR = (cx > cy) ? cx : cy;

    uint16_t t = elapsedMs / 10;

    for (int16_t y = 0; y < height; y++) {
        for (int16_t x = 0; x < width; x++) {

            int16_t dx = x - cx;
            int16_t dy = y - cy;

            int16_t dist = (dx * dx + dy * dy);
            if (dist > maxR * maxR) {
                pixels[y * width + x] = LedColor(0, 0, 0);
                continue;
            }

            int32_t angle = (int32_t)dy * 1000 / (dist > 0 ? dist : 1);
            angle = angle + t;

            uint8_t r = (uint8_t)(getAbsSine(angle) >> 7);
            uint8_t g = (uint8_t)(getAbsSine(angle + 10922) >> 7);
            uint8_t b = (uint8_t)(getAbsSine(angle + 21845) >> 7);

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

#include "led_effects.h"

void calculateWater(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    uint16_t t = elapsedMs * 15;

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            uint16_t a1 = (x * 180 + t);
            uint16_t a2 = (y * 220 - t * 2);

            int16_t v = (getSignedSine(a1) + getSignedSine(a2)) / 2;

            uint8_t b = (uint8_t)((getAbsSine(v)) >> 7);
            uint8_t g = b >> 1;
            uint8_t r = 0;

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

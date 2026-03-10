#include "led_effects.h"

void calculateWheel(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    int16_t cx = width / 2;
    int16_t cy = height / 2;

    uint16_t t = elapsedMs * 10;

    for (int16_t y = 0; y < height; y++) {
        for (int16_t x = 0; x < width; x++) {

            int16_t dx = x - cx;
            int16_t dy = y - cy;

            // crude integer atan2 replacement using sine table
            uint16_t angle = (uint16_t)((dx * 256 + dy * 512) & 0xFFFF);

            uint16_t hue = angle + t;

            uint8_t r = (uint8_t)(getAbsSine(hue) >> 7);
            uint8_t g = (uint8_t)(getAbsSine(hue + 21845) >> 7); // +120°
            uint8_t b = (uint8_t)(getAbsSine(hue + 43690) >> 7); // +240°

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

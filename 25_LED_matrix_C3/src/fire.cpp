#include "led_effects.h"

void calculateFire(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    uint16_t t = elapsedMs / 8;

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            uint16_t a = (x * 40 + t + y * 80);
            uint16_t s = getAbsSine(a);

            uint16_t fade = ((height - y) * 1024) / height;
            uint16_t heat = (s * fade) >> 15;

            uint8_t r = heat >> 7;
            uint8_t g = (heat >> 10);
            uint8_t b = 0;

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

#include "led_effects.h"

void calculateFire(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    uint16_t t = elapsedMs * 25;

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            uint16_t a = (x * 300 + t + y * 500);
            uint16_t s = getAbsSine(a); // 0..32767

            // Fire fades upward
            uint16_t fade = ((height - y) * 1024);
            uint16_t heat = (s * fade) >> 15; // 0..32767

            uint8_t r = heat >> 7;
            uint8_t g = (heat >> 9);
            uint8_t b = (heat >> 12);

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

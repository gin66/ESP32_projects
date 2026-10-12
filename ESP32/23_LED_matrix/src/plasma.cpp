#include "led_effects.h"

void calculatePlasma(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    uint16_t t = (elapsedMs * 20) & 0xFFFF;  // animation speed

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            uint16_t a1 = (x * 200 + t);
            uint16_t a2 = (y * 180 + t * 2);
            uint16_t a3 = ((x + y) * 150 + t * 3);

            int16_t v =  getSignedSine(a1)
                        + getSignedSine(a2)
                        + getSignedSine(a3);

            v /= 3; // still -32767..32767

            uint8_t r = (uint8_t)((getAbsSine(v + 0x0000)) >> 7);
            uint8_t g = (uint8_t)((getAbsSine(v + 0x5555)) >> 7);
            uint8_t b = (uint8_t)((getAbsSine(v + 0xAAAA)) >> 7);

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }
}

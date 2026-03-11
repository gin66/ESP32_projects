#include "led_effects.h"
#include <stdlib.h>

#define STAR_COUNT 64

static int16_t starX[STAR_COUNT];
static int16_t starY[STAR_COUNT];
static uint16_t starZ[STAR_COUNT];
static bool starInit = false;

void initStars(uint16_t width, uint16_t height) {
    for (int i = 0; i < STAR_COUNT; i++) {
        starX[i] = (rand() % 2000) - 1000;
        starY[i] = (rand() % 2000) - 1000;
        starZ[i] = (rand() % 2000) + 500;
    }
    starInit = true;
}

void calculateStarfield(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!starInit) {
        initStars(width, height);
    }

    for (uint16_t i = 0; i < width * height; i++)
        pixels[i] = LedColor(0, 0, 0);

    for (int i = 0; i < STAR_COUNT; i++) {

        starZ[i] -= 2;
        if (starZ[i] < 100) {
            starX[i] = (rand() % 2000) - 1000;
            starY[i] = (rand() % 2000) - 1000;
            starZ[i] = (rand() % 1500) + 500;
        }

        int16_t sx = (starX[i] * 80) / starZ[i] + width / 2;
        int16_t sy = (starY[i] * 80) / starZ[i] + height / 2;

        if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
            uint8_t brightness = (uint8_t)(255 - (starZ[i] >> 4));
            pixels[sy * width + sx] = LedColor(brightness, brightness, brightness);
        }
    }
}

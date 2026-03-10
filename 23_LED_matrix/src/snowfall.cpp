#include "led_effects.h"
#include <stdlib.h>

#define SNOW_COUNT 32

static int16_t snowX[SNOW_COUNT];
static int16_t snowY[SNOW_COUNT];
static uint16_t snowPhase[SNOW_COUNT];
static bool snowInit = false;

void initSnow(uint16_t width, uint16_t height) {
    for (int i = 0; i < SNOW_COUNT; i++) {
        snowX[i] = rand() % width;
        snowY[i] = rand() % height;
        snowPhase[i] = rand() & 0xFFFF;
    }
}

void calculateSnowfall(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!snowInit) {
        initSnow(width, height);
        snowInit = true;
    }

    // Clear screen
    for (uint16_t i = 0; i < width * height; i++)
        pixels[i] = LedColor(0,0,0);

    for (int i = 0; i < SNOW_COUNT; i++) {

        // Drift left/right using sine
        int16_t drift = getSignedSine(snowPhase[i]) >> 14; // -2..2
        snowX[i] += drift;

        // Fall speed
        snowY[i] += 1;

        // Wrap around
        if (snowY[i] >= height) {
            snowY[i] = 0;
            snowX[i] = rand() % width;
        }

        if (snowX[i] < 0) snowX[i] += width;
        if (snowX[i] >= width) snowX[i] -= width;

        // Draw snowflake
        pixels[snowY[i] * width + snowX[i]] = LedColor(200, 200, 255);

        // Animate phase
        snowPhase[i] += 300;
    }
}

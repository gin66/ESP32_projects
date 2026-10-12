#include "led_effects.h"
#include <stdlib.h>

#define SNOW_COUNT 64

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
    snowInit = true;
}

void calculateSnowfall(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!snowInit) {
        initSnow(width, height);
    }

    for (uint16_t i = 0; i < width * height; i++)
        pixels[i] = LedColor(0, 0, 0);

    for (int i = 0; i < SNOW_COUNT; i++) {

        int16_t drift = getSignedSine(snowPhase[i]) >> 12;
        snowX[i] += drift;

        snowY[i] += 1;
        if (snowY[i] >= height) {
            snowY[i] = 0;
            snowX[i] = rand() % width;
        }

        if (snowX[i] < 0) snowX[i] += width;
        if (snowX[i] >= width) snowX[i] -= width;

        if (snowY[i] >= 0 && snowY[i] < height && snowX[i] >= 0 && snowX[i] < width) {
            pixels[snowY[i] * width + snowX[i]] = LedColor(200, 200, 255);
        }

        snowPhase[i] += 40;
    }
}

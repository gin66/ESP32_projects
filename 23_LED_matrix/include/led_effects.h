#pragma once

#include "led_config.h"

uint16_t xyToIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

LedColor hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t brightness);

void calculateAllPixels(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs,
    LedMode mode,
    uint8_t brightness,
    uint8_t staticR, uint8_t staticG, uint8_t staticB,
    BgStyle bgStyle,
    uint32_t bgSpeed,
    uint16_t waveLength
);

void drawClockOverlay(LedColor* pixels, uint16_t width, uint16_t height, uint8_t clockBrightness);

uint32_t estimateCurrent(LedColor* pixels, uint16_t pixelCount, uint8_t numPanels, uint8_t scale);

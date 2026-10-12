#pragma once

#include "led_config.h"
#include "led_effects_utils.h"

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

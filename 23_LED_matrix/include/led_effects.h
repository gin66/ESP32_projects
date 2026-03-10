#pragma once

#include "led_config.h"

uint16_t xyToIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

LedColor hsvToRgb(uint8_t h, uint8_t s, uint8_t v);

void calculateAllPixels(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs,
    LedMode mode,
    uint8_t brightness,
    uint8_t staticR, uint8_t staticG, uint8_t staticB,
    uint32_t rainbowSpeed
);

#pragma once

#include "led_config.h"
#include <stdint.h>

uint16_t xyToIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

LedColor hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t brightness);

uint32_t estimateCurrent(LedColor* pixels, uint16_t pixelCount, uint8_t numPanels, uint8_t scale);

int16_t getSignedSine(uint16_t angle);
uint16_t getAbsSine(uint16_t angle);

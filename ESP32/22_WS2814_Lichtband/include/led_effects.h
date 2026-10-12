#pragma once

#include "led_config.h"

LedColor correctColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w);

LedColor hsvToRgbwUint16(uint16_t hue, uint8_t sat, uint8_t val, uint8_t brightness);

LedColor calculateLedColor(
    uint16_t ledIndex,
    uint16_t ledCount,
    unsigned long elapsedMs,
    LedMode mode,
    uint8_t brightness,
    const WaveConfig& waveCfg,
    LedState& state,
    uint8_t staticR, uint8_t staticG, uint8_t staticB, uint8_t staticW,
    uint32_t rainbowSpeed,
    uint32_t whiteSpeed
);

void calculateAllLeds(
    LedColor* leds,
    uint16_t ledCount,
    unsigned long elapsedMs,
    LedMode mode,
    uint8_t brightness,
    const WaveConfig& waveCfg,
    LedState& state,
    uint8_t staticR, uint8_t staticG, uint8_t staticB, uint8_t staticW,
    uint32_t rainbowSpeed,
    uint32_t whiteSpeed
);

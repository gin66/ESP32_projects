#pragma once

#include <stdint.h>

#define LED_DEFAULT_COUNT 80
#define WAVE_CONFIG_VERSION 0x0101

enum LedMode {
    ModeOff,
    ModeStatic,
    ModeRainbow,
    ModeRainbowWave,
    ModeWhite
};

struct WaveConfig {
    uint16_t version;
    uint16_t waveWidth;
    uint32_t waveSpeed;
    bool bidirectional;
    float acceleration;
    uint16_t staticSegmentCount;
    uint16_t staticSegmentOffset;
    bool need_store;
    
    void init() {
        version = WAVE_CONFIG_VERSION;
        waveWidth = LED_DEFAULT_COUNT / 5;
        waveSpeed = 2000;
        bidirectional = true;
        acceleration = 0.0f;
        staticSegmentCount = LED_DEFAULT_COUNT;
        staticSegmentOffset = 0;
        need_store = false;
    }
    
    WaveConfig() { init(); }
};

struct LedColor {
    uint8_t W;
    uint8_t R;
    uint8_t G;
    uint8_t B;
    
    LedColor() : W(0), R(0), G(0), B(0) {}
    LedColor(uint8_t w, uint8_t r, uint8_t g, uint8_t b) : W(w), R(r), G(g), B(b) {}
};

struct LedState {
    unsigned long lastWaveUpdate;
    uint32_t wavePosition;
    int16_t waveVelocityQ8;
    
    LedState() : lastWaveUpdate(0), wavePosition(0), waveVelocityQ8(256) {}
};

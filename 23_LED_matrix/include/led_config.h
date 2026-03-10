#pragma once

#include <stdint.h>

#ifndef MATRIX_WIDTH
#define MATRIX_WIDTH 32
#endif

#ifndef MATRIX_HEIGHT
#define MATRIX_HEIGHT 32
#endif

#ifndef MATRIX_PIXEL_COUNT
#define MATRIX_PIXEL_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)
#endif

enum LedMode {
    ModeOff,
    ModeStatic,
    ModeRainbow,
    ModeWhite,
    ModeScanner,
    ModeRawScanner
};

enum BgStyle {
    BgSolid,
    BgTrapezoid,
    BgRings,
    BgWave,
    BgRipple
};

struct MatrixConfig {
    uint16_t version;
    uint8_t brightness;
    bool doubleBuffer;
    
    void init() {
        version = 0x0101;
        brightness = 128;
        doubleBuffer = true;
    }
    
    MatrixConfig() { init(); }
};

struct LedColor {
    uint8_t R;
    uint8_t G;
    uint8_t B;
    
    LedColor() : R(0), G(0), B(0) {}
    LedColor(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b) {}
};

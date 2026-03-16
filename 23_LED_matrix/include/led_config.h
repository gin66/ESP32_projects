#pragma once

#include <stdint.h>

#define PANELS 4

// Panel rotation pattern:
// PANEL_ROTATION_ODD: panels 2/4 rotated, 1/3 not (panelIndex % 2 == 1)
// default: panels 1/3 rotated, 2/4 not (panelIndex % 2 == 0)
#define PANEL_ROTATION_ODD
#define MATRIX_LED_PIN 16
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT (8 * PANELS)
#define MATRIX_PIXEL_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)

enum LedMode {
    ModeOff,
    ModeStatic,
    ModeRainbow,
    ModeWhite,
    ModeScanner,
    ModeFire,
    ModeWater,
    ModePlasma,
    ModeStarfield,
    ModeLavalamp,
    ModeAutomata,
    ModeSnowfall,
    ModeWheel,
    ModeMorphingClock
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

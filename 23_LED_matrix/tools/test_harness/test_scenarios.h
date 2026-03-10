#pragma once

#include "../../include/led_config.h"
#include <string>
#include <vector>

struct TestScenario {
    std::string name;
    std::string description;
    LedMode mode;
    uint8_t brightness;
    uint8_t staticR, staticG, staticB;
    BgStyle bgStyle;
    uint32_t bgSpeed;
    uint16_t waveLength;
    unsigned long durationMs;
    unsigned long frameIntervalMs;
};

inline TestScenario createScenario_Off() {
    TestScenario s;
    s.name = "Off";
    s.description = "All LEDs off";
    s.mode = ModeOff;
    s.brightness = 128;
    s.staticR = s.staticG = s.staticB = 0;
    s.bgStyle = BgSolid;
    s.bgSpeed = 3000;
    s.waveLength = 256;
    s.durationMs = 500;
    s.frameIntervalMs = 50;
    return s;
}

inline TestScenario createScenario_Static(uint8_t brightness) {
    TestScenario s;
    s.name = "Static RGB (brightness=" + std::to_string((int)brightness) + ")";
    s.description = "Static white color";
    s.mode = ModeStatic;
    s.brightness = brightness;
    s.staticR = s.staticG = s.staticB = 255;
    s.bgStyle = BgSolid;
    s.bgSpeed = 3000;
    s.waveLength = 256;
    s.durationMs = 500;
    s.frameIntervalMs = 50;
    return s;
}

inline TestScenario createScenario_Rainbow_Solid(BgStyle style, const std::string& styleName, uint32_t bgSpeed, uint16_t waveLength) {
    TestScenario s;
    s.name = "Rainbow " + styleName + " (speed=" + std::to_string(bgSpeed) + ", wave=" + std::to_string(waveLength) + ")";
    s.description = "Rainbow mode with " + styleName + " background";
    s.mode = ModeRainbow;
    s.brightness = 255;
    s.staticR = s.staticG = s.staticB = 0;
    s.bgStyle = style;
    s.bgSpeed = bgSpeed;
    s.waveLength = waveLength;
    s.durationMs = bgSpeed * 2;
    s.frameIntervalMs = 50;
    return s;
}

inline TestScenario createScenario_White(uint8_t brightness) {
    TestScenario s;
    s.name = "White (brightness=" + std::to_string(brightness) + ")";
    s.description = "Pulsing white";
    s.mode = ModeWhite;
    s.brightness = brightness;
    s.staticR = s.staticG = s.staticB = 255;
    s.bgStyle = BgSolid;
    s.bgSpeed = 8000;
    s.waveLength = 256;
    s.durationMs = 8000;
    s.frameIntervalMs = 50;
    return s;
}

inline TestScenario createScenario_Scanner() {
    TestScenario s;
    s.name = "Scanner";
    s.description = "Single LED scanner";
    s.mode = ModeScanner;
    s.brightness = 255;
    s.staticR = s.staticG = s.staticB = 0;
    s.bgStyle = BgSolid;
    s.bgSpeed = 3000;
    s.waveLength = 256;
    s.durationMs = 2000;
    s.frameIntervalMs = 50;
    return s;
}

inline std::vector<TestScenario> getAllScenarios() {
    std::vector<TestScenario> scenarios;
    
    scenarios.push_back(createScenario_Off());
    scenarios.push_back(createScenario_Static(16));
    scenarios.push_back(createScenario_Static(64));
    scenarios.push_back(createScenario_Static(255));
    scenarios.push_back(createScenario_White(255));
    scenarios.push_back(createScenario_Scanner());
    
    scenarios.push_back(createScenario_Rainbow_Solid(BgSolid, "Solid", 3000, 256));
    scenarios.push_back(createScenario_Rainbow_Solid(BgTrapezoid, "Trapezoid", 3000, 256));
    scenarios.push_back(createScenario_Rainbow_Solid(BgTrapezoid, "Trapezoid", 3000, 64));
    scenarios.push_back(createScenario_Rainbow_Solid(BgTrapezoid, "Trapezoid", 3000, 512));
    scenarios.push_back(createScenario_Rainbow_Solid(BgRings, "Rings", 3000, 256));
    scenarios.push_back(createScenario_Rainbow_Solid(BgRings, "Rings", 3000, 64));
    scenarios.push_back(createScenario_Rainbow_Solid(BgRings, "Rings", 3000, 512));
    scenarios.push_back(createScenario_Rainbow_Solid(BgWave, "Wave", 3000, 256));
    scenarios.push_back(createScenario_Rainbow_Solid(BgWave, "Wave", 3000, 64));
    scenarios.push_back(createScenario_Rainbow_Solid(BgWave, "Wave", 3000, 512));
    scenarios.push_back(createScenario_Rainbow_Solid(BgRipple, "Ripple", 3000, 256));
    
    return scenarios;
}

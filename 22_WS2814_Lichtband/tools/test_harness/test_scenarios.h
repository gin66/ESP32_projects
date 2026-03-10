#pragma once

#include "../../include/led_config.h"
#include <string>
#include <vector>

struct TestScenario {
    std::string name;
    std::string description;
    LedMode mode;
    uint8_t brightness;
    WaveConfig waveCfg;
    uint8_t staticR, staticG, staticB, staticW;
    uint32_t rainbowSpeed;
    uint32_t whiteSpeed;
    unsigned long durationMs;
    unsigned long frameIntervalMs;
    int16_t flickerThresholdOverride;  // -1 = use default, >0 = per-scenario
};

// Compute expected max per-frame delta for a moving wave effect.
// The steepest part of the quadratic falloff produces the largest deltas.
// For quadratic intensity: I = (1 - d/w)^2, the derivative peaks at d=0
// with dI/dd = -2/w. Per frame the wave moves: ledCount * frameInterval / waveSpeed LEDs.
// Max channel delta ≈ brightness * 2 * motion / waveWidth (with safety margin 1.5x).
inline int16_t computeWaveThreshold(uint8_t brightness, uint16_t waveWidth,
                                     uint32_t waveSpeed, uint16_t ledCount,
                                     unsigned long frameIntervalMs, float accelFactor = 1.0f) {
    float motion = (float)ledCount * frameIntervalMs / waveSpeed * accelFactor;
    float maxDelta = (float)brightness * 2.0f * motion / waveWidth;
    // 2.5x margin: wave intensity gradient + hue rotation both contribute
    int16_t threshold = (int16_t)(maxDelta * 2.5f);
    return threshold < 15 ? 15 : threshold;
}

inline TestScenario createScenario_Off(uint8_t brightness) {
    TestScenario s;
    s.name = "Off (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "All LEDs off";
    s.mode = ModeOff;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 500;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_Static_RGB(uint8_t brightness) {
    TestScenario s;
    s.name = "Static RGB (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Static red color";
    s.mode = ModeStatic;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = 255;
    s.staticG = 0;
    s.staticB = 0;
    s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 500;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_Static_W(uint8_t brightness) {
    TestScenario s;
    s.name = "Static W (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Static white (W channel only)";
    s.mode = ModeStatic;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = 0;
    s.staticG = 0;
    s.staticB = 0;
    s.staticW = 255;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 500;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_Static_RGBW(uint8_t brightness) {
    TestScenario s;
    s.name = "Static RGBW (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Static mixed RGBW color";
    s.mode = ModeStatic;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = 255;
    s.staticG = 128;
    s.staticB = 64;
    s.staticW = 100;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 500;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_Rainbow(uint8_t brightness) {
    TestScenario s;
    s.name = "Rainbow (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Full rainbow cycle";
    s.mode = ModeRainbow;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 3000;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_White(uint8_t brightness) {
    TestScenario s;
    s.name = "White (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Sine wave on white channel";
    s.mode = ModeWhite;
    s.brightness = brightness;
    s.waveCfg.init();
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 8000;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = -1;
    return s;
}

inline TestScenario createScenario_RainbowWave_Bidirectional(uint8_t brightness) {
    TestScenario s;
    s.name = "RainbowWave Bidir (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Bidirectional rainbow wave";
    s.mode = ModeRainbowWave;
    s.brightness = brightness;
    s.waveCfg.init();
    s.waveCfg.bidirectional = true;
    s.waveCfg.waveSpeed = 2000;
    s.waveCfg.waveWidth = 16;
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 4000;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = computeWaveThreshold(
        brightness, s.waveCfg.waveWidth, s.waveCfg.waveSpeed,
        LED_DEFAULT_COUNT, s.frameIntervalMs);
    return s;
}

inline TestScenario createScenario_RainbowWave_Unidirectional(uint8_t brightness) {
    TestScenario s;
    s.name = "RainbowWave Unidir (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Unidirectional rainbow wave";
    s.mode = ModeRainbowWave;
    s.brightness = brightness;
    s.waveCfg.init();
    s.waveCfg.bidirectional = false;
    s.waveCfg.acceleration = 0.0f;
    s.waveCfg.waveSpeed = 2000;
    s.waveCfg.waveWidth = 16;
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 4000;
    s.frameIntervalMs = 20;
    s.flickerThresholdOverride = computeWaveThreshold(
        brightness, s.waveCfg.waveWidth, s.waveCfg.waveSpeed,
        LED_DEFAULT_COUNT, s.frameIntervalMs);
    return s;
}

inline TestScenario createScenario_RainbowWave_Acceleration(uint8_t brightness) {
    TestScenario s;
    s.name = "RainbowWave Accel (brightness=" + std::to_string(brightness * 100 / 255) + "%)";
    s.description = "Unidirectional with acceleration";
    s.mode = ModeRainbowWave;
    s.brightness = brightness;
    s.waveCfg.init();
    s.waveCfg.bidirectional = false;
    s.waveCfg.acceleration = 0.5f;
    s.waveCfg.waveSpeed = 2000;
    s.waveCfg.waveWidth = 16;
    s.staticR = s.staticG = s.staticB = s.staticW = 0;
    s.rainbowSpeed = 3000;
    s.whiteSpeed = 8000;
    s.durationMs = 4000;
    s.frameIntervalMs = 20;
    // Acceleration can reach max velocity (5x=1280/256), use higher accel factor.
    // Extra margin for velocity reset discontinuity at cycle wrap.
    s.flickerThresholdOverride = computeWaveThreshold(
        brightness, s.waveCfg.waveWidth, s.waveCfg.waveSpeed,
        LED_DEFAULT_COUNT, s.frameIntervalMs, 5.5f);
    return s;
}

inline std::vector<TestScenario> getAllScenarios() {
    std::vector<TestScenario> scenarios;
    
    uint8_t brightnessLevels[] = {64, 128, 255};
    
    for (uint8_t br : brightnessLevels) {
        scenarios.push_back(createScenario_Off(br));
        scenarios.push_back(createScenario_Static_RGB(br));
        scenarios.push_back(createScenario_Static_W(br));
        scenarios.push_back(createScenario_Static_RGBW(br));
        scenarios.push_back(createScenario_Rainbow(br));
        scenarios.push_back(createScenario_White(br));
        scenarios.push_back(createScenario_RainbowWave_Bidirectional(br));
        scenarios.push_back(createScenario_RainbowWave_Unidirectional(br));
        scenarios.push_back(createScenario_RainbowWave_Acceleration(br));
    }
    
    return scenarios;
}

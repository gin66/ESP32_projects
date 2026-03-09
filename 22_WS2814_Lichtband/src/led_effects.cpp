#include "led_effects.h"
#include <cstdlib>

static const uint16_t sineTable45[] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987, 9121, 10252,
    11380, 12505, 13626, 14742, 15855, 16962, 18064, 19161, 20252, 21336,
    22415, 23486, 24550, 25607, 26656, 27697, 28729, 29753, 30767, 31772,
    32768, 32768
};

static inline uint16_t getSine(uint16_t angle) {
    angle = angle % 360;
    if (angle < 90) return sineTable45[angle / 2];
    if (angle < 180) return sineTable45[90 - ((angle - 90) / 2) - 1];
    if (angle < 270) return 32768 - sineTable45[(angle - 180) / 2];
    return 32768 - sineTable45[90 - ((angle - 270) / 2) - 1];
}

static inline uint16_t getSineNormalized(uint16_t angle) {
    return getSine(angle);
}

LedColor correctColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    return LedColor(w, r, g, b);
}

LedColor hsvToRgbwUint16(uint16_t hue, uint8_t sat, uint8_t val, uint8_t brightness) {
    uint8_t r, g, b;
    
    uint8_t sector = (uint8_t)((uint32_t)hue * 6 / 65536);
    uint16_t f = (uint32_t)hue * 6 - (uint32_t)sector * 65536;
    
    uint8_t p = (uint16_t)val * (255 - sat) / 255;
    uint8_t q = (uint16_t)val * (255 - (uint32_t)f * sat / 65536) / 255;
    uint8_t t = (uint16_t)val * (255 - (uint32_t)(65536 - f) * sat / 65536) / 255;
    
    switch (sector % 6) {
        case 0: r = val; g = t; b = p; break;
        case 1: r = q; g = val; b = p; break;
        case 2: r = p; g = val; b = t; break;
        case 3: r = p; g = q; b = val; break;
        case 4: r = t; g = p; b = val; break;
        default: r = val; g = p; b = q; break;
    }
    
    r = (uint16_t)r * brightness / 255;
    g = (uint16_t)g * brightness / 255;
    b = (uint16_t)b * brightness / 255;
    
    return correctColor(r, g, b, 0);
}

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
) {
    switch (mode) {
        case ModeOff:
            return LedColor(0, 0, 0, 0);
            
        case ModeStatic: {
            if (ledIndex >= waveCfg.staticSegmentOffset && 
                ledIndex < waveCfg.staticSegmentOffset + waveCfg.staticSegmentCount &&
                ledIndex < ledCount) {
                return correctColor(
                    (uint16_t)staticR * brightness / 255,
                    (uint16_t)staticG * brightness / 255,
                    (uint16_t)staticB * brightness / 255,
                    (uint16_t)staticW * brightness / 255
                );
            }
            return LedColor(0, 0, 0, 0);
        }
        
        case ModeRainbow: {
            uint32_t timeOffset = (elapsedMs % rainbowSpeed) * 65536 / rainbowSpeed;
            uint16_t positionOffset = (uint32_t)ledIndex * 65536 / ledCount;
            uint16_t hue = (timeOffset + positionOffset) % 65536;
            return hsvToRgbwUint16(hue, 255, 255, brightness);
        }
        
        case ModeRainbowWave: {
            uint16_t waveWidth = waveCfg.waveWidth;
            uint16_t wavePos16;
            
            if (waveCfg.bidirectional) {
                uint32_t cycleDuration = (uint32_t)waveCfg.waveSpeed * 2;
                uint32_t cyclePos = elapsedMs % cycleDuration;
                
                if (cyclePos < waveCfg.waveSpeed) {
                    wavePos16 = cyclePos * 65536 / waveCfg.waveSpeed;
                } else {
                    wavePos16 = 65535 - (cyclePos - waveCfg.waveSpeed) * 65536 / waveCfg.waveSpeed;
                }
            } else {
                if (waveCfg.acceleration != 0) {
                    uint32_t dt = elapsedMs - state.lastWaveUpdate;
                    if (dt > 20) {
                        int16_t accelQ8 = (int16_t)(waveCfg.acceleration * 256);
                        state.waveVelocityQ8 += (accelQ8 * dt) / 1000;
                        if (state.waveVelocityQ8 < 26) state.waveVelocityQ8 = 26;
                        if (state.waveVelocityQ8 > 1280) state.waveVelocityQ8 = 1280;
                        
                        state.wavePosition += (uint32_t)state.waveVelocityQ8 * dt * 65536 / waveCfg.waveSpeed / 256;
                        state.lastWaveUpdate = elapsedMs;
                    }
                    
                    if (state.wavePosition >= 65536) {
                        state.wavePosition = 0;
                        state.waveVelocityQ8 = 256;
                    }
                    wavePos16 = state.wavePosition;
                } else {
                    wavePos16 = (elapsedMs % waveCfg.waveSpeed) * 65536 / waveCfg.waveSpeed;
                }
            }
            
            uint16_t hueOffset = (elapsedMs % rainbowSpeed) * 65536 / rainbowSpeed;
            uint16_t baseHue = (hueOffset + wavePos16) % 65536;
            uint16_t centerPos = (uint32_t)wavePos16 * (ledCount - 1) / 65536;
            
            uint16_t dist = abs((int)ledIndex - (int)centerPos);
            uint16_t intensity16 = 0;
            if (dist < waveWidth) {
                uint16_t t = (uint32_t)dist * 65536 / waveWidth;
                uint16_t inv = 65535 - t;
                intensity16 = (uint32_t)inv * inv / 65536;
            }
            uint8_t intensity = intensity16 * 255 / 65536;
            
            uint16_t hue = (baseHue + (uint32_t)ledIndex * 65536 / ledCount) % 65536;
            return hsvToRgbwUint16(hue, 255, intensity, brightness);
        }
        
        case ModeWhite: {
            uint16_t angle = (elapsedMs % whiteSpeed) * 360 / whiteSpeed;
            uint16_t sineVal = getSineNormalized(angle);
            uint8_t w = (uint32_t)sineVal * brightness / 32768;
            return correctColor(0, 0, 0, w);
        }
        
        default:
            return LedColor(0, 0, 0, 0);
    }
}

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
) {
    for (uint16_t i = 0; i < ledCount; i++) {
        leds[i] = calculateLedColor(
            i, ledCount, elapsedMs, mode, brightness,
            waveCfg, state,
            staticR, staticG, staticB, staticW,
            rainbowSpeed, whiteSpeed
        );
    }
}

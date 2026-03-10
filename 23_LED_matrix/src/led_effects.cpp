#include "led_effects.h"
#include <Arduino.h>
#include <stdio.h>
#include "../lib/font5x7/font5x7.h"

extern void calculateFire(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateWater(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculatePlasma(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateStarfield(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateLavalamp(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateAutomata(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateSnowfall(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateWheel(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);
extern void calculateMorphingClock(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs);

#define MAX_RIPPLES 3

struct Ripple {
    int16_t x;
    int16_t y;
    uint32_t startTime;
    bool active;
};

static Ripple ripples[MAX_RIPPLES];
static uint32_t lastRippleTime = 0;

static void drawChar(LedColor* pixels, uint16_t width, uint16_t height,
                     char c, uint16_t ox, uint16_t oy,
                     uint8_t r, uint8_t g, uint8_t b) {
    uint8_t idx = (uint8_t)c;
    if (idx > 95) return;
    
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colData = pgm_read_byte(&font5x7[idx][col]);
        for (uint8_t row = 0; row < 7; row++) {
            if (colData & (1 << row)) {
                uint16_t px = ox + col;
                uint16_t py = oy + row;
                if (px < width && py < height) {
                    pixels[py * width + px] = LedColor(r, g, b);
                }
            }
        }
    }
}

static void drawString(LedColor* pixels, uint16_t width, uint16_t height,
                       const char* str, uint16_t ox, uint16_t oy,
                       uint8_t r, uint8_t g, uint8_t b) {
    while (*str) {
        drawChar(pixels, width, height, *str, ox, oy, r, g, b);
        ox += 6;
        str++;
    }
}

static uint16_t distanceApprox(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    int32_t dx = (int32_t)x1 - (int32_t)x2;
    int32_t dy = (int32_t)y1 - (int32_t)y2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (uint16_t)((dx > dy) ? (dx + dy / 2) : (dy + dx / 2));
}

static void drawBackground(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs,
    BgStyle style,
    uint32_t bgSpeed,
    uint16_t waveLength,
    uint8_t brightness
) {
    uint16_t totalPixels = width * height;
    int16_t centerX = width / 2;
    int16_t centerY = height / 2;
    
    uint16_t timeHue = 0;
    if (bgSpeed > 0) {
        timeHue = (uint16_t)((elapsedMs % bgSpeed) * 65536UL / bgSpeed);
    }
    
    LedColor* p = pixels;
    
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            LedColor color;
            
            switch (style) {
                case BgSolid:
                    color = hsvToRgb(timeHue, 255, 255, brightness);
                    break;
                
                case BgTrapezoid: {
                    uint32_t pos = (uint32_t)(x + y) * 65536UL * waveLength / 256 / (width + height);
                    uint16_t hue = (uint16_t)((pos + timeHue) % 65536);
                    color = hsvToRgb(hue, 255, 255, brightness);
                    break;
                }
                
                case BgRings: {
                    uint16_t maxDist = (width > height ? width : height) / 2;
                    uint16_t dist = distanceApprox(x, y, centerX, centerY);
                    uint32_t scaledDist = (uint32_t)dist * waveLength * 65536UL / 256 / (maxDist > 0 ? maxDist : 1);
                    uint16_t hue = (uint16_t)((timeHue + scaledDist) % 65536);
                    color = hsvToRgb(hue, 255, 255, brightness);
                    break;
                }
                
                case BgWave: {
                    uint32_t scaledX = (uint32_t)x * waveLength * 256UL / width;
                    uint16_t waveAngle = (uint16_t)((elapsedMs % bgSpeed) * 360 / bgSpeed);
                    int16_t waveSine = getSignedSine(waveAngle);
                    uint16_t waveOffset = (uint16_t)(((int32_t)waveSine + 32768) * 8192 / 65536);
                    uint16_t hue = (uint16_t)((timeHue + scaledX + waveOffset) % 65536);
                    color = hsvToRgb(hue, 255, 255, brightness);
                    break;
                }
                
                case BgRipple: {
                    uint32_t rippleDuration = 6000;
                    uint8_t bestIntensity = 0;
                    uint16_t bestHue = timeHue;
                    
                    for (uint8_t r = 0; r < MAX_RIPPLES; r++) {
                        if (!ripples[r].active) continue;
                        
                        uint32_t rippleAge = elapsedMs - ripples[r].startTime;
                        if (rippleAge > rippleDuration) {
                            ripples[r].active = false;
                            continue;
                        }
                        
                        uint16_t dist = distanceApprox(x, y, ripples[r].x, ripples[r].y);
                        
                        uint16_t ringPos = (uint16_t)(rippleAge * waveLength / 4096);
                        int16_t distFromRing = (int16_t)dist - (int16_t)ringPos;
                        if (distFromRing < 0) distFromRing = -distFromRing;
                        
                        uint16_t waveletPhase = (dist * 32) & 0xFF;
                        uint8_t wavelet = (waveletPhase < 128) ? (waveletPhase * 2) : ((256 - waveletPhase) * 2);
                        
                        if (distFromRing < 8) {
                            uint8_t ageFade = (uint8_t)(255 - (rippleAge * 255 / rippleDuration));
                            uint8_t distFade = (uint8_t)(255 - distFromRing * 32);
                            uint8_t intensity = (uint8_t)((ageFade * distFade * wavelet) / 65025);
                            
                            if (intensity > bestIntensity) {
                                bestIntensity = intensity;
                                bestHue = (timeHue + dist * 8) & 0xFFFF;
                            }
                        }
                    }
                    
                    uint8_t finalIntensity = bestIntensity > 16 ? bestIntensity : 16;
                    color = hsvToRgb(bestHue, 255, finalIntensity, brightness);
                    break;
                }
                
                default:
                    color = LedColor(0, 0, 0);
            }
            
            *p++ = color;
        }
    }
}

static void addRipple(uint16_t x, uint16_t y, unsigned long currentTime) {
    for (uint8_t i = 0; i < MAX_RIPPLES; i++) {
        if (!ripples[i].active) {
            ripples[i].x = (int16_t)x;
            ripples[i].y = (int16_t)y;
            ripples[i].startTime = currentTime;
            ripples[i].active = true;
            break;
        }
    }
}

static void updateRipples(unsigned long currentTime, uint16_t width, uint16_t height, uint32_t speed) {
    uint32_t spawnInterval = speed > 0 ? speed : 3000;
    if (spawnInterval < 500) spawnInterval = 500;
    if (lastRippleTime == 0 || currentTime - lastRippleTime > spawnInterval) {
        lastRippleTime = currentTime;
        uint16_t rx = (uint16_t)(rand() % width);
        uint16_t ry = (uint16_t)(rand() % height);
        addRipple(rx, ry, currentTime);
    }
}

void drawClockOverlay(LedColor* pixels, uint16_t width, uint16_t height, uint8_t clockBrightness) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        
        uint16_t strW = 5 * 6 - 1;
        uint16_t ox = (width - strW) / 2;
        uint16_t oy = (height - 7) / 2;
        
        drawString(pixels, width, height, timeStr, ox, oy, clockBrightness, clockBrightness, clockBrightness);
    }
}

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
) {
    uint16_t totalPixels = width * height;
    
    switch (mode) {
        case ModeFire:
            calculateFire(pixels, width, height, elapsedMs);
            return;
        case ModeWater:
            calculateWater(pixels, width, height, elapsedMs);
            return;
        case ModePlasma:
            calculatePlasma(pixels, width, height, elapsedMs);
            return;
        case ModeStarfield:
            calculateStarfield(pixels, width, height, elapsedMs);
            return;
        case ModeLavalamp:
            calculateLavalamp(pixels, width, height, elapsedMs);
            return;
        case ModeAutomata:
            calculateAutomata(pixels, width, height, elapsedMs);
            return;
        case ModeSnowfall:
            calculateSnowfall(pixels, width, height, elapsedMs);
            return;
        case ModeWheel:
            calculateWheel(pixels, width, height, elapsedMs);
            return;
        case ModeMorphingClock:
            calculateMorphingClock(pixels, width, height, elapsedMs);
            return;
        default:
            break;
    }
    
    for (uint16_t i = 0; i < totalPixels; i++) {
        switch (mode) {
            case ModeOff:
                pixels[i] = LedColor(0, 0, 0);
                break;
                
            case ModeStatic: {
                uint16_t br = (uint16_t)brightness;
                pixels[i] = LedColor(
                    (uint16_t)staticR * br / 255,
                    (uint16_t)staticG * br / 255,
                    (uint16_t)staticB * br / 255
                );
                break;
            }
            
            case ModeRainbow:
                break;
            
            case ModeWhite: {
                uint16_t angle = (uint16_t)((elapsedMs % 8000) * 360 / 8000);
                uint16_t sineVal = getAbsSine(angle);
                uint8_t wave = (uint8_t)(sineVal * 255 / 32768);
                uint16_t br = (uint16_t)wave * brightness / 255;
                pixels[i] = LedColor(br, br, br);
                break;
            }
            
            case ModeScanner: {
                uint32_t msPerLed = 200;
                uint16_t totalLeds = width * height;
                uint16_t logicalPos = (uint16_t)((elapsedMs / msPerLed) % totalLeds);
                pixels[i] = (i == logicalPos) ? LedColor(brightness, brightness, brightness) : LedColor(0, 0, 0);
                break;
            }
            
            default:
                pixels[i] = LedColor(0, 0, 0);
        }
    }
    
    if (mode == ModeRainbow) {
        drawBackground(pixels, width, height, elapsedMs, bgStyle, bgSpeed, waveLength, brightness);
        if (bgStyle == BgRipple) {
            updateRipples(elapsedMs, width, height, bgSpeed);
        }
    }
}

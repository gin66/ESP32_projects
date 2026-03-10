#include "led_effects.h"
#include <Arduino.h>
#include "../lib/font5x7/font5x7.h"

#define MAX_RIPPLES 3

static const uint16_t sineTable45[] = {
    0, 1144, 2287, 3425, 4560, 5689, 6813, 7927, 9030, 10126,
    11207, 12275, 13322, 14350, 15356, 16384, 17364, 18322, 19260, 20173,
    21060, 21929, 22767, 23578, 24359, 25107, 25825, 26510, 27161, 27777,
    28378, 28902, 29409, 29879, 30310, 30700, 31050, 31358, 31625, 31849,
    32031, 32170, 32266, 32318, 32327, 32768
};

static inline uint16_t getSine(uint16_t angle) {
    angle = angle % 360;
    if (angle < 90) return sineTable45[angle / 2];
    if (angle < 180) return sineTable45[(180 - angle) / 2];
    if (angle < 270) return sineTable45[(angle - 180) / 2];
    return sineTable45[(360 - angle) / 2];
}

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

uint16_t xyToIndex(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint16_t panelHeight = 8;
    uint16_t panelIndex = y / panelHeight;
    uint16_t localY = y % panelHeight;
    
    uint16_t xi = x;
    uint16_t yi = localY;
    
    bool rotated = (panelIndex % 2 == 0);
    if (rotated) {
        xi = width - 1 - xi;
        yi = panelHeight - 1 - yi;
    }
    
    if (xi % 2 == 1) {
        yi = panelHeight - 1 - yi;
    }
    
    uint16_t index = panelIndex * panelHeight * width;
    index += xi * panelHeight;
    index += yi;
    
    return index;
}

static uint8_t gamma8(uint8_t val) {
    return val;
}

LedColor hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t brightness) {
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
    
    uint8_t gam = gamma8(brightness);
    return LedColor(
        (uint16_t)r * gam / 255,
        (uint16_t)g * gam / 255,
        (uint16_t)b * gam / 255
    );
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
                    uint32_t scaledX = (uint32_t)x * waveLength * 65536UL / 256 / width;
                    uint16_t waveAngle = (uint16_t)((elapsedMs % bgSpeed) * 360 / bgSpeed);
                    uint16_t waveSine = getSine(waveAngle);
                    uint16_t waveOffset = (uint16_t)((uint32_t)waveSine * 8192 / 32768);
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
    if (clockBrightness == 0) return;
    
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
                uint16_t sineVal = getSine(angle);
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

uint32_t estimateCurrent(LedColor* pixels, uint16_t pixelCount, uint8_t numPanels, uint8_t scale) {
    uint32_t baseCurrent = 322000 + (numPanels - 1) * 129000;
    
    const uint32_t currentPerRed = 11900;
    const uint32_t currentPerGreen = 11200;
    const uint32_t currentPerBlue = 11800;
    
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    for (uint16_t i = 0; i < pixelCount; i++) {
        sumR += ((uint16_t)pixels[i].R)*scale/255;
        sumG += ((uint16_t)pixels[i].G)*scale/255;
        sumB += ((uint16_t)pixels[i].B)*scale/255;
    }
    
    uint32_t ledCurrent = (sumR * currentPerRed + sumG * currentPerGreen + sumB * currentPerBlue) / 255;
    
    return baseCurrent + ledCurrent;
}

#include "led_effects.h"
#include <cmath>

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

LedColor hsvToRgb(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t r, g, b;
    
    uint8_t sector = h / 43;
    uint16_t f = (uint16_t)h * 6 - (uint16_t)sector * 256;
    uint8_t p = (uint16_t)v * (255 - s) / 255;
    uint8_t q = (uint16_t)v * (255 - (uint16_t)f * s / 256) / 255;
    uint8_t t = (uint16_t)v * (255 - (uint16_t)(256 - f) * s / 256) / 255;
    
    switch (sector) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    
    return LedColor(r, g, b);
}

void calculateAllPixels(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs,
    LedMode mode,
    uint8_t brightness,
    uint8_t staticR, uint8_t staticG, uint8_t staticB,
    uint32_t rainbowSpeed
) {
    uint16_t totalPixels = width * height;
    
    for (uint16_t i = 0; i < totalPixels; i++) {
        uint16_t x = i % width;
        uint16_t y = i / width;
        
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
            
            case ModeRainbow: {
                uint32_t timeOffset = (elapsedMs % rainbowSpeed) * 256 / rainbowSpeed;
                uint16_t hue = (timeOffset + (uint32_t)x * 256 / width + (uint32_t)y * 256 / height) % 256;
                LedColor c = hsvToRgb(hue, 255, 255);
                uint16_t br = (uint16_t)brightness;
                pixels[i] = LedColor(
                    (uint16_t)c.R * br / 255,
                    (uint16_t)c.G * br / 255,
                    (uint16_t)c.B * br / 255
                );
                break;
            }
            
            case ModeRainbowWave: {
                uint32_t wavePos = (elapsedMs % 5000) * 256 / 5000;
                uint16_t dist = abs((int)x - (int)(width / 2)) + abs((int)y - (int)(height / 2));
                uint16_t waveDist = (dist + wavePos) % 256;
                uint8_t intensity = (waveDist < 128) ? waveDist * 2 : (256 - waveDist) * 2;
                uint8_t hue = (wavePos + dist) % 256;
                LedColor c = hsvToRgb(hue, 255, intensity);
                uint16_t br = (uint16_t)brightness;
                pixels[i] = LedColor(
                    (uint16_t)c.R * br / 255,
                    (uint16_t)c.G * br / 255,
                    (uint16_t)c.B * br / 255
                );
                break;
            }
            
            case ModeWhite: {
                uint16_t angle = (elapsedMs % 8000) * 360 / 8000;
                uint8_t wave = (uint8_t)((sin((float)angle * 0.017453f) + 1.0f) * 127.5f);
                uint16_t br = (uint16_t)wave * brightness / 255;
                pixels[i] = LedColor(br, br, br);
                break;
            }
            
            case ModeClock: {
                uint16_t centerX = width / 2;
                uint16_t centerY = height / 2;
                int dx = (int)x - (int)centerX;
                int dy = (int)y - (int)centerY;
                float dist = sqrtf((float)dx * dx + (float)dy * dy);
                float angle = atan2f((float)dy, (float)dx);
                if (angle < 0) angle += 2.0f * 3.14159f;
                
                uint8_t angleByte = (uint8_t)(angle / (2.0f * 3.14159f) * 256.0f);
                uint8_t distByte = (uint8_t)(dist * 2);
                
                LedColor c = hsvToRgb(angleByte, 200, distByte);
                uint16_t br = (uint16_t)brightness;
                pixels[i] = LedColor(
                    (uint16_t)c.R * br / 255,
                    (uint16_t)c.G * br / 255,
                    (uint16_t)c.B * br / 255
                );
                break;
            }
            
            case ModeScanner: {
                uint32_t msPerLed = 200;
                uint16_t totalLeds = width * height;
                uint16_t logicalPos = (elapsedMs / msPerLed) % totalLeds;
                pixels[i] = (i == logicalPos) ? LedColor(brightness, brightness, brightness) : LedColor(0, 0, 0);
                break;
            }
            
            default:
                pixels[i] = LedColor(0, 0, 0);
        }
    }
}

// Based on measurements:
// - 100% Brightness red=255 for 32x8 LEDs: 3.37A
// - 100% Brightness blue=255 for 32x8 LEDs: 3.34A  
// - 100% Brightness green=255 for 32x8 LEDs: 3.19A
// - all off with 32x8: 322mA
// - all off with 32x24: 580mA
// - 10% Brightness, white, 32x24 => 3.4A
// Returns current in uA (1 unit = 1uA)
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

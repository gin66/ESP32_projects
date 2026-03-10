#include "led_effects.h"
#include <cmath>
#include <Arduino.h>
#include "../lib/font5x7/font5x7.h"

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
    uint16_t x = ox;
    while (*str) {
        drawChar(pixels, width, height, *str, x, oy, r, g, b);
        x += 6;
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
    
    return LedColor(
        (uint16_t)r * brightness / 255,
        (uint16_t)g * brightness / 255,
        (uint16_t)b * brightness / 255
    );
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
                uint32_t timeOffset = (elapsedMs % rainbowSpeed) * 65536 / rainbowSpeed;
                uint16_t hue = (timeOffset + (uint32_t)x * 65536 / width + (uint32_t)y * 65536 / height) % 65536;
                pixels[i] = hsvToRgb(hue, 255, 255, brightness);
                break;
            }
            
            case ModeRainbowWave: {
                uint32_t wavePos = (elapsedMs % 5000) * 65536 / 5000;
                uint16_t dist = abs((int)x - (int)(width / 2)) + abs((int)y - (int)(height / 2));
                uint16_t waveDist = (dist * 256 + wavePos) % 65536;
                uint8_t intensity = (waveDist < 32768) ? waveDist * 2 / 256 : (65536 - waveDist) * 2 / 256;
                uint16_t hue = (wavePos + dist * 256) % 65536;
                pixels[i] = hsvToRgb(hue, 255, intensity, brightness);
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
                uint32_t timeOffset = (elapsedMs % rainbowSpeed) * 65536 / rainbowSpeed;
                uint16_t hue = (timeOffset + (uint32_t)x * 65536 / width + (uint32_t)y * 65536 / height) % 65536;
                pixels[i] = hsvToRgb(hue, 255, 128, brightness);
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
    
    if (mode == ModeClock) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeStr[6];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            
            uint16_t strW = 5 * 6 - 1;
            uint16_t ox = (width - strW) / 2;
            uint16_t oy = (height - 7) / 2;
            
            uint16_t cw = brightness * 3 / 4;
            drawString(pixels, width, height, timeStr, ox, oy, cw, cw, cw);
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

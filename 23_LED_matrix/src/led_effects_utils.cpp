#include "led_effects_utils.h"
#include <stdlib.h>

static const int16_t sineTable45[] = {
    0, 1144, 2287, 3425, 4560, 5689, 6813, 7927, 9030, 10126,
    11207, 12275, 13322, 14350, 15356, 16384, 17364, 18322, 19260, 20173,
    21060, 21929, 22767, 23578, 24359, 25107, 25825, 26510, 27161, 27777,
    28378, 28902, 29409, 29879, 30310, 30700, 31050, 31358, 31625, 31849,
    32031, 32170, 32266, 32318, 32327, 32767
};

int16_t getSignedSine(uint16_t angle) {
    angle = angle % 360;
    if (angle < 90) return sineTable45[angle / 2];
    if (angle < 180) return sineTable45[(180 - angle) / 2];
    if (angle < 270) return -sineTable45[(angle - 180) / 2];
    return -sineTable45[(360 - angle) / 2];
}

uint16_t getAbsSine(uint16_t angle) {
    int16_t s = getSignedSine(angle);
    if (s < 0) {
       s = -s;
    }
    return (uint16_t)s;
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

uint32_t estimateCurrent(LedColor* pixels, uint16_t pixelCount, uint8_t numPanels, uint8_t scale) {
    uint32_t baseCurrent = 322000 + (numPanels - 1) * 129000;
    
    const uint16_t currentPerRedDiv255 = 11900 / 255;
    const uint16_t currentPerGreenDiv255 = 11200 / 255;
    const uint16_t currentPerBlueDiv255 = 11800 / 255;
    
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    for (uint16_t i = 0; i < pixelCount; i++) {
        sumR += ((uint16_t)pixels[i].R) * scale / 255;
        sumG += ((uint16_t)pixels[i].G) * scale / 255;
        sumB += ((uint16_t)pixels[i].B) * scale / 255;
    }
    
    uint32_t ledCurrent = sumR * currentPerRedDiv255 + sumG * currentPerGreenDiv255 + sumB * currentPerBlueDiv255;
    
    return baseCurrent + ledCurrent;
}

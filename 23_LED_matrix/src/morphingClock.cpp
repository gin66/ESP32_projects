#include "led_effects.h"
#include <Arduino.h>

static const uint8_t digitBitmap[10][24] = {
{0b0111110,0b1100011,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1100011,0b0111110,0b0000000,0b0000000},
{0b0001000,0b0011000,0b0111000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0001000,0b0011100,0b0111110,0b0000000,0b0000000,0b0000000},
{0b0111110,0b1100011,0b1000001,0b0000001,0b0000010,0b0000100,0b0001000,0b0010000,0b0100000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1100011,0b1111111,0b0000000,0b0000000,0b0000000},
{0b0111110,0b1100011,0b1000001,0b0000001,0b0000010,0b0000100,0b0001000,0b0011100,0b0000010,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b1000001,0b1100011,0b0111110,0b0000000,0b0000000,0b0000000,0b0000000},
{0b0000010,0b0000110,0b0001010,0b0010010,0b0100010,0b1000010,0b1000010,0b1000010,0b1111111,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000010,0b0000000,0b0000000,0b0000000,0b0000000},
{0b1111111,0b1000000,0b1000000,0b1000000,0b1111110,0b0000011,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b1000001,0b1100011,0b0111110,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000},
{0b0011110,0b0100000,0b1000000,0b1000000,0b1000000,0b1111110,0b1100011,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1100011,0b0111110,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000},
{0b1111111,0b0000011,0b0000010,0b0000100,0b0001000,0b0010000,0b0100000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b1000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000},
{0b0111110,0b1100011,0b1000001,0b1000001,0b1100011,0b0111110,0b1100011,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1000001,0b1100011,0b0111110,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000},
{0b0111110,0b1100011,0b1000001,0b1000001,0b1100011,0b0111111,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b0000001,0b1000011,0b1100010,0b0111100,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000,0b0000000}
};

struct Pt { int8_t x, y; };

static Pt srcPts[4][64];
static Pt dstPts[4][64];
static uint8_t srcCount[4];
static uint8_t dstCount[4];
static uint8_t prevDigit[4] = {0,0,0,0};
static uint8_t nextDigit[4] = {0,0,0,0};
static unsigned long lastChange[4] = {0,0,0,0};
static bool morphing[4] = {false,false,false,false};

void extractDigitPoints(uint8_t digit, Pt* out, uint8_t* count) {
    *count = 0;
    for (uint8_t y = 0; y < 24; y++) {
        uint8_t row = digitBitmap[digit][y];
        for (uint8_t x = 0; x < 7; x++) {
            if (row & (1 << (6 - x))) {
                out[*count].x = x;
                out[*count].y = y;
                (*count)++;
            }
        }
    }
}

void matchPoints(Pt* s, uint8_t sc, Pt* d, uint8_t dc, Pt* outS, Pt* outD) {
    uint8_t count = (sc > dc ? sc : dc);
    for (uint8_t i = 0; i < count; i++) {
        outS[i] = s[i % sc];
        outD[i] = d[i % dc];
    }
}

uint8_t easeInOut(uint8_t t) {
    uint16_t T = t;
    uint32_t v = T * T * (3*255 - 2*T);
    return v >> 16;
}

LedColor hsvToRgb2(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t region = h / 43;
    uint8_t remainder = (h - region * 43) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0: return LedColor(v, t, p);
        case 1: return LedColor(q, v, p);
        case 2: return LedColor(p, v, t);
        case 3: return LedColor(p, q, v);
        case 4: return LedColor(t, p, v);
        default: return LedColor(v, p, q);
    }
}

void drawMorphDigit(LedColor* pixels, uint16_t width, uint8_t digitIndex, uint8_t offsetX, uint8_t offsetY, unsigned long elapsedMs, struct tm* timeinfo) {
    uint8_t cur = nextDigit[digitIndex];
    uint8_t newDigit;
    
    if (digitIndex == 0) newDigit = timeinfo->tm_hour / 10;
    if (digitIndex == 1) newDigit = timeinfo->tm_hour % 10;
    if (digitIndex == 2) newDigit = timeinfo->tm_min / 10;
    if (digitIndex == 3) newDigit = timeinfo->tm_min % 10;

    if (newDigit != cur && !morphing[digitIndex]) {
        prevDigit[digitIndex] = cur;
        nextDigit[digitIndex] = newDigit;
        lastChange[digitIndex] = elapsedMs;
        morphing[digitIndex] = true;
        extractDigitPoints(prevDigit[digitIndex], srcPts[digitIndex], &srcCount[digitIndex]);
        extractDigitPoints(nextDigit[digitIndex], dstPts[digitIndex], &dstCount[digitIndex]);
        matchPoints(srcPts[digitIndex], srcCount[digitIndex], dstPts[digitIndex], dstCount[digitIndex], srcPts[digitIndex], dstPts[digitIndex]);
    }

    if (!morphing[digitIndex]) {
        extractDigitPoints(nextDigit[digitIndex], srcPts[digitIndex], &srcCount[digitIndex]);
        dstCount[digitIndex] = srcCount[digitIndex];
        for (uint8_t i = 0; i < srcCount[digitIndex]; i++) {
            dstPts[digitIndex][i] = srcPts[digitIndex][i];
        }
    }

    uint8_t t = 255;
    if (morphing[digitIndex]) {
        uint16_t dt = elapsedMs - lastChange[digitIndex];
        if (dt >= 800) {
            morphing[digitIndex] = false;
        } else {
            t = (dt * 256) / 800;
        }
    }

    uint8_t eased = easeInOut(t);
    uint8_t baseHue = (elapsedMs >> 4) & 0xFF;
    uint8_t count = (srcCount[digitIndex] > dstCount[digitIndex] ? srcCount[digitIndex] : dstCount[digitIndex]);

    for (uint8_t i = 0; i < count; i++) {
        int16_t x = srcPts[digitIndex][i].x + (((dstPts[digitIndex][i].x - srcPts[digitIndex][i].x) * eased) >> 8);
        int16_t y = srcPts[digitIndex][i].y + (((dstPts[digitIndex][i].y - srcPts[digitIndex][i].y) * eased) >> 8);
        x += offsetX;
        y += offsetY;
        if (x >= 0 && x < width && y >= 0 && y < 23) {
            uint8_t hue = baseHue + x*3 + y*5;
            LedColor c = hsvToRgb2(hue, 255, eased);
            pixels[y * width + x] = c;
        }
    }
}

void drawProgressBar(LedColor* pixels, uint16_t width, unsigned long elapsedMs, struct tm* timeinfo) {
    uint8_t sec = timeinfo->tm_sec;
    
    int16_t peakPos;
    int16_t maxPos = width - 6;
    if (sec < 30) {
        peakPos = (sec * maxPos) / 30;
    } else {
        peakPos = ((60 - sec) * maxPos) / 30;
    }

    uint16_t pulseAngle = (elapsedMs % 1000) * 360 / 1000;
    uint8_t pulse = (uint16_t)getAbsSine(pulseAngle) * 90 / 32768 + 26;

    uint8_t baseHue = (elapsedMs >> 2) & 0xFF;

    for (int i = 0; i < 5; i++) {
        int x = peakPos + i;
        if (x >= 0 && x < width) {
            uint8_t shape = (uint16_t)getAbsSine(i * 90) >> 7;
            uint8_t hue = baseHue + x * 8;
            uint8_t brightness = (shape * pulse) >> 8;
            LedColor c = hsvToRgb2(hue, 255, brightness);
            pixels[23 * width + x] = c;
        }
    }
}

void calculateMorphingClock(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs) {
    for (uint16_t i = 0; i < width * height; i++)
        pixels[i] = LedColor(0,0,0);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    uint8_t yOff = 0;
    drawMorphDigit(pixels, width, 0,  0, yOff, elapsedMs, &timeinfo);
    drawMorphDigit(pixels, width, 1,  8, yOff, elapsedMs, &timeinfo);
    drawMorphDigit(pixels, width, 2, 17, yOff, elapsedMs, &timeinfo);
    drawMorphDigit(pixels, width, 3, 25, yOff, elapsedMs, &timeinfo);
    
    drawProgressBar(pixels, width, elapsedMs, &timeinfo);
}

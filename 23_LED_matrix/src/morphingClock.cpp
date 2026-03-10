#include "led_effects.h"

static const uint8_t digitMask[10] = {
    0b0111111,
    0b0000110,
    0b1011011,
    0b1001111,
    0b1100110,
    0b1101101,
    0b1111101,
    0b0000111,
    0b1111111,
    0b1101111
};

struct SegmentRect {
    uint8_t x1, y1, x2, y2;
};

#ifdef MATRIX32x32
static const SegmentRect segments[7] = {
    { 6,  2, 26,  6},  // 0 top
    { 4,  4,  8, 14},  // 1 upper-left
    {24,  4, 28, 14},  // 2 upper-right
    { 6, 14, 26, 18},  // 3 middle
    { 4, 16,  8, 28},  // 4 lower-left
    {24, 16, 28, 28},  // 5 lower-right
    { 6, 26, 26, 30}   // 6 bottom
};
#endif

static const SegmentRect segments[7] = {
    { 6,  1, 26,  4},
    { 4,  3,  8, 10},
    {24,  3, 28, 10},
    { 6, 10, 26, 13},
    { 4, 12,  8, 20},
    {24, 12, 28, 20},
    { 6, 19, 26, 22}
};

static uint8_t prevDigit = 0;
static uint8_t nextDigit = 0;
static unsigned long lastChange = 0;

uint8_t clamp255(int16_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

void drawSegment(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    const SegmentRect* s,
    uint8_t brightness)
{
    for (uint8_t y = s->y1; y <= s->y2 && y < height; y++) {
        for (uint8_t x = s->x1; x <= s->x2 && x < width; x++) {
            pixels[y * width + x] = LedColor(brightness, brightness, brightness);
        }
    }
}

void calculateMorphingClock(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    // Clear screen
    for (uint16_t i = 0; i < width * height; i++)
        pixels[i] = LedColor(0,0,0);

    // Determine current digit (0–9)
    uint8_t currentDigit = (elapsedMs / 1000) % 10;

    // Detect change
    if (currentDigit != nextDigit) {
        prevDigit = nextDigit;
        nextDigit = currentDigit;
        lastChange = elapsedMs;
    }

    // Morph progress 0–255
    uint16_t dt = elapsedMs - lastChange;
    uint8_t progress = dt >= 800 ? 255 : (dt * 256 / 800);

    uint8_t maskPrev = digitMask[prevDigit];
    uint8_t maskNext = digitMask[nextDigit];

    for (uint8_t s = 0; s < 7; s++) {

        uint8_t onPrev = (maskPrev >> s) & 1;
        uint8_t onNext = (maskNext >> s) & 1;

        int16_t brightness = 0;

        if (onPrev && onNext) {
            // Always on
            brightness = 255;
        }
        else if (onPrev && !onNext) {
            // Fade out
            brightness = 255 - progress;
        }
        else if (!onPrev && onNext) {
            // Fade in
            brightness = progress;
        }
        else {
            // Off
            brightness = 0;
        }

        brightness = clamp255(brightness);

        if (brightness > 0)
            drawSegment(pixels, width, height, &segments[s], brightness);
    }
}

#include "led_effects.h"
#include <stdlib.h>

#define BLOB_COUNT 4

static int16_t blobX[BLOB_COUNT];
static int16_t blobY[BLOB_COUNT];
static uint16_t blobPhase[BLOB_COUNT];
static bool blobInit = false;

void initBlobs(uint16_t width, uint16_t height) {
    for (int i = 0; i < BLOB_COUNT; i++) {
        blobX[i] = rand() % width;
        blobY[i] = rand() % height;
        blobPhase[i] = rand() & 0xFFFF;
    }
}

void calculateLavalamp(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!blobInit) {
        initBlobs(width, height);
        blobInit = true;
    }

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            uint32_t field = 0;

            for (int i = 0; i < BLOB_COUNT; i++) {

                // Blob movement (circular)
                int16_t bx = blobX[i] + (getSignedSine(blobPhase[i]) >> 13);
                int16_t by = blobY[i] + (getSignedSine(blobPhase[i] + 16384) >> 13);

                int16_t dx = x - bx;
                int16_t dy = y - by;

                uint16_t dist2 = (dx*dx + dy*dy + 1);

                // Field strength = 20000 / dist² (approx)
                field += (20000 / dist2);
            }

            // Map field to color
            uint8_t r = (field > 255 ? 255 : field);
            uint8_t g = (r >> 1);
            uint8_t b = (r >> 3);

            pixels[y * width + x] = LedColor(r, g, b);
        }
    }

    // Animate blob phases
    for (int i = 0; i < BLOB_COUNT; i++)
        blobPhase[i] += 200;
}

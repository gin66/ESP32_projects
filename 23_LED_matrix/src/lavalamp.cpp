#include "led_effects.h"
#include <stdlib.h>

#define BLOB_COUNT 5

static int16_t blobX[BLOB_COUNT];
static int16_t blobY[BLOB_COUNT];
static int16_t blobVX[BLOB_COUNT];
static int16_t blobVY[BLOB_COUNT];
static bool blobInit = false;

void initBlobs(uint16_t width, uint16_t height) {
    for (int i = 0; i < BLOB_COUNT; i++) {
        blobX[i] = rand() % width;
        blobY[i] = rand() % height;
        blobVX[i] = (rand() % 5) - 2;
        blobVY[i] = (rand() % 5) - 2;
        if (blobVX[i] == 0) blobVX[i] = 1;
        if (blobVY[i] == 0) blobVY[i] = 1;
    }
    blobInit = true;
}

void calculateLavalamp(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!blobInit) {
        initBlobs(width, height);
    }

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {

            int32_t field = 0;

            for (int i = 0; i < BLOB_COUNT; i++) {
                int16_t dx = x - blobX[i];
                int16_t dy = y - blobY[i];
                int32_t dist2 = dx * dx + dy * dy;
                if (dist2 < 1) dist2 = 1;
                
                field += (800 / dist2);
            }

            if (field > 120) {
                uint8_t intensity = (field > 255) ? 255 : (uint8_t)field;
                uint8_t r = intensity;
                uint8_t g = (intensity * 200) >> 8;
                uint8_t b = (intensity * 50) >> 8;
                pixels[y * width + x] = LedColor(r, g, b);
            } else if (field > 40) {
                uint8_t intensity = (field * 2);
                if (intensity > 60) intensity = 60;
                pixels[y * width + x] = LedColor(intensity, intensity >> 1, 0);
            } else {
                pixels[y * width + x] = LedColor(2, 1, 0);
            }
        }
    }

    for (int i = 0; i < BLOB_COUNT; i++) {
        blobX[i] += blobVX[i];
        blobY[i] += blobVY[i];

        if (blobX[i] <= 2 || blobX[i] >= width - 3) {
            blobVX[i] = -blobVX[i];
            blobX[i] += blobVX[i];
        }
        if (blobY[i] <= 2 || blobY[i] >= height - 3) {
            blobVY[i] = -blobVY[i];
            blobY[i] += blobVY[i];
        }
    }
}

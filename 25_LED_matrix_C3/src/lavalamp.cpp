#include "led_effects.h"
#include <stdlib.h>
#include <math.h>

#define BLOB_COUNT 6
#define BOTTOM_ROWS 2

struct Blob {
    float x;
    float y;
    float vy;
    float radius;
    uint8_t hue;
    bool active;
    float wobblePhase;
    float wobbleSpeed;
};

static Blob blobs[BLOB_COUNT];
static bool blobInit = false;
static unsigned long lastSpawn = 0;
static unsigned long lastUpdate = 0;

void initBlobs(uint16_t width, uint16_t height) {
    for (int i = 0; i < BLOB_COUNT; i++) {
        blobs[i].active = false;
    }
    blobInit = true;
    lastSpawn = 0;
    lastUpdate = 0;
}

void spawnBlob(uint16_t width, uint16_t height, int index) {
    blobs[index].x = 3 + (rand() % (width - 6));
    blobs[index].y = height - BOTTOM_ROWS - 1;
    blobs[index].vy = 0.05 + (rand() % 50) / 1000.0;
    blobs[index].radius = 2.0 + (rand() % 20) / 10.0;
    blobs[index].hue = 20 + (rand() % 30);
    blobs[index].active = true;
    blobs[index].wobblePhase = (rand() % 628) / 100.0;
    blobs[index].wobbleSpeed = 0.01 + (rand() % 20) / 1000.0;
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

    float dt = (elapsedMs - lastUpdate) / 100.0;
    if (dt > 3.0) dt = 3.0;
    lastUpdate = elapsedMs;

    if (elapsedMs - lastSpawn > 2000 + (rand() % 3000)) {
        for (int i = 0; i < BLOB_COUNT; i++) {
            if (!blobs[i].active) {
                spawnBlob(width, height, i);
                lastSpawn = elapsedMs;
                break;
            }
        }
    }

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            pixels[y * width + x] = LedColor(8, 2, 0);
        }
    }

    for (uint16_t y = height - BOTTOM_ROWS; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t intensity = 200 + (rand() % 55);
            pixels[y * width + x] = LedColor(intensity, (intensity * 180) >> 8, 0);
        }
    }

    for (int i = 0; i < BLOB_COUNT; i++) {
        Blob& b = blobs[i];
        if (!b.active) continue;

        b.wobblePhase += b.wobbleSpeed * dt;
        float wobble = sin(b.wobblePhase) * 0.3;
        b.x += wobble * dt * 0.5;

        if (b.x < 2) b.x = 2;
        if (b.x > width - 3) b.x = width - 3;

        b.y -= b.vy * dt;

        if (b.y < 3) {
            b.vy = -b.vy * 0.3;
            b.y = 3;
        }

        if (b.y >= height - BOTTOM_ROWS - 1) {
            b.active = false;
        }
    }

    for (int i = 0; i < BLOB_COUNT; i++) {
        Blob& b = blobs[i];
        if (!b.active) continue;

        for (uint16_t y = 0; y < height - BOTTOM_ROWS; y++) {
            for (uint16_t x = 0; x < width; x++) {
                float dx = x - b.x;
                float dy = y - b.y;
                float dist2 = dx * dx + dy * dy;
                float radius2 = b.radius * b.radius;

                if (dist2 < radius2 * 4) {
                    float dist = sqrt(dist2);
                    float glow = 1.0 - (dist / (b.radius * 2));
                    if (glow < 0) glow = 0;

                    uint8_t intensity = (uint8_t)(glow * 255);
                    uint8_t r = intensity;
                    uint8_t g = (intensity * 180) >> 8;
                    uint8_t b_color = (intensity * 30) >> 8;

                    LedColor& p = pixels[y * width + x];
                    if (intensity > p.R) {
                        p = LedColor(r, g, b_color);
                    }
                }
            }
        }
    }
}

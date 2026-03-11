#include "led_effects.h"
#include <stdlib.h>

static uint8_t grid[32][32];
static uint8_t nextGrid[32][32];
static bool lifeInit = false;
static bool stable = false;
static unsigned long stableTime = 0;
static uint8_t fadeBrightness = 255;
static uint32_t hashHistory[4];
static uint8_t hashIdx = 0;

uint32_t calcHash() {
    uint32_t h = 0;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            h = h * 31 + grid[y][x];
        }
    }
    return h;
}

bool checkCycle(uint32_t h) {
    for (int i = 0; i < 4; i++) {
        if (hashHistory[i] == h) return true;
    }
    return false;
}

void initLife() {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            grid[y][x] = (rand() & 1);
    stable = false;
    fadeBrightness = 255;
    for (int i = 0; i < 4; i++) hashHistory[i] = 0;
    hashIdx = 0;
}

void stepLife() {
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {

            uint8_t n = 0;

            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (!(dx == 0 && dy == 0))
                        n += grid[(y+dy+32)%32][(x+dx+32)%32];

            uint8_t alive = grid[y][x];

            if (alive && (n == 2 || n == 3))
                nextGrid[y][x] = 1;
            else if (!alive && n == 3)
                nextGrid[y][x] = 1;
            else
                nextGrid[y][x] = 0;
        }
    }

    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            grid[y][x] = nextGrid[y][x];
}

void calculateAutomata(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!lifeInit) {
        initLife();
        lifeInit = true;
    }

    if (stable) {
        if (elapsedMs - stableTime > 10000) {
            initLife();
        } else {
            uint16_t elapsed = elapsedMs - stableTime;
            fadeBrightness = 255 - (elapsed * 255 / 10000);
        }
    } else {
        static unsigned long last = 0;
        if (elapsedMs - last > 400) {
            stepLife();
            last = elapsedMs;
            
            uint32_t h = calcHash();
            if (checkCycle(h)) {
                stable = true;
                stableTime = elapsedMs;
            } else {
                hashHistory[hashIdx] = h;
                hashIdx = (hashIdx + 1) & 3;
            }
        }
    }

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            if (grid[y][x]) {
                pixels[y * width + x] = LedColor(0, fadeBrightness, fadeBrightness / 5);
            } else {
                pixels[y * width + x] = LedColor(0, 0, 0);
            }
        }
    }
}

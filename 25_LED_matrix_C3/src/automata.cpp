#include "led_effects.h"
#include <stdlib.h>
#include <cstring>

static uint8_t* grid = nullptr;
static uint8_t* nextGrid = nullptr;
static bool lifeInit = false;
static bool stable = false;
static unsigned long stableTime = 0;
static uint8_t fadeBrightness = 255;
static uint32_t hashHistory[4];
static uint8_t hashIdx = 0;
static uint16_t gridW = 0;
static uint16_t gridH = 0;

static void allocGrids(uint16_t w, uint16_t h) {
    if (gridW == w && gridH == h && grid) return;
    free(grid);
    free(nextGrid);
    grid = (uint8_t*)calloc(w * h, 1);
    nextGrid = (uint8_t*)calloc(w * h, 1);
    gridW = w;
    gridH = h;
}

uint32_t calcHash() {
    uint32_t h = 0;
    for (int i = 0; i < gridW * gridH; i++) {
        h = h * 31 + grid[i];
    }
    return h;
}

bool checkCycle(uint32_t h) {
    for (int i = 0; i < 4; i++) {
        if (hashHistory[i] == h) return true;
    }
    return false;
}

void initLife(uint16_t w, uint16_t h) {
    allocGrids(w, h);
    for (int i = 0; i < w * h; i++)
        grid[i] = (rand() & 1);
    stable = false;
    fadeBrightness = 255;
    for (int i = 0; i < 4; i++) hashHistory[i] = 0;
    hashIdx = 0;
}

void stepLife(uint16_t w, uint16_t h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (!(dx == 0 && dy == 0))
                        n += grid[((y+dy+h)%h)*w + (x+dx+w)%w];

            uint8_t alive = grid[y*w+x];
            if (alive && (n == 2 || n == 3))
                nextGrid[y*w+x] = 1;
            else if (!alive && n == 3)
                nextGrid[y*w+x] = 1;
            else
                nextGrid[y*w+x] = 0;
        }
    }
    memcpy(grid, nextGrid, w * h);
}

void calculateAutomata(
    LedColor* pixels,
    uint16_t width,
    uint16_t height,
    unsigned long elapsedMs)
{
    if (!lifeInit) {
        initLife(width, height);
        lifeInit = true;
    }

    if (stable) {
        if (elapsedMs - stableTime > 10000) {
            initLife(width, height);
        } else {
            uint16_t elapsed = elapsedMs - stableTime;
            fadeBrightness = 255 - (elapsed * 255 / 10000);
        }
    } else {
        static unsigned long last = 0;
        if (elapsedMs - last > 400) {
            stepLife(width, height);
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
            if (grid[y*width+x]) {
                pixels[y * width + x] = LedColor(0, fadeBrightness, fadeBrightness / 5);
            } else {
                pixels[y * width + x] = LedColor(0, 0, 0);
            }
        }
    }
}

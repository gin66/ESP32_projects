#include "led_effects.h"
#include <stdlib.h>

static uint8_t grid[32][32];
static uint8_t nextGrid[32][32];
static bool lifeInit = false;

void initLife() {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            grid[y][x] = (rand() & 1);
}

void stepLife() {
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {

            uint8_t n = 0;

            // Count neighbors
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (!(dx == 0 && dy == 0))
                        n += grid[(y+dy+32)%32][(x+dx+32)%32];

            uint8_t alive = grid[y][x];

            // Apply rules
            if (alive && (n == 2 || n == 3))
                nextGrid[y][x] = 1;
            else if (!alive && n == 3)
                nextGrid[y][x] = 1;
            else
                nextGrid[y][x] = 0;
        }
    }

    // Swap buffers
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

    // Update every 150 ms
    static unsigned long last = 0;
    if (elapsedMs - last > 150) {
        stepLife();
        last = elapsedMs;
    }

    // Render
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            if (grid[y][x])
                pixels[y * width + x] = LedColor(0, 255, 80);
            else
                pixels[y * width + x] = LedColor(0, 0, 0);
        }
    }
}

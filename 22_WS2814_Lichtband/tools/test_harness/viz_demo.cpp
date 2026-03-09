#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

struct RgbwColor {
    uint8_t W, R, G, B;
    RgbwColor() : W(0), R(0), G(0), B(0) {}
    RgbwColor(uint8_t w, uint8_t r, uint8_t g, uint8_t b) : W(w), R(r), G(g), B(b) {}
};

enum class VizMode {
    RGB,
    Grayscale
};

void printColorBlockRGB(const RgbwColor& color) {
    uint8_t r = std::min(255, (int)color.R + (int)color.W);
    uint8_t g = std::min(255, (int)color.G + (int)color.W);
    uint8_t b = std::min(255, (int)color.B + (int)color.W);
    printf("\033[38;2;%d;%d;%dm██\033[0m", r, g, b);
}

void printColorBlockGrayscale(const RgbwColor& color) {
    uint8_t gray = std::min(255, (int)color.W);
    printf("\033[38;2;%d;%d;%dm██\033[0m", gray, gray, gray);
}

void printLedStrip(const RgbwColor* leds, uint16_t count, VizMode mode, uint16_t ledsPerLine = 40) {
    for (uint16_t i = 0; i < count; i++) {
        if (mode == VizMode::RGB) {
            printColorBlockRGB(leds[i]);
        } else {
            printColorBlockGrayscale(leds[i]);
        }
        if ((i + 1) % ledsPerLine == 0) {
            printf("\n");
        }
    }
    if (count % ledsPerLine != 0) {
        printf("\n");
    }
}

RgbwColor hsvToRgb(uint16_t hue, uint8_t sat, uint8_t val) {
    uint8_t sector = (uint8_t)((uint32_t)hue * 6 / 65536);
    uint16_t f = (uint32_t)hue * 6 - (uint32_t)sector * 65536;
    
    uint8_t p = (uint16_t)val * (255 - sat) / 255;
    uint8_t q = (uint16_t)val * (255 - (uint32_t)f * sat / 65536) / 255;
    uint8_t t = (uint16_t)val * (255 - (uint32_t)(65536 - f) * sat / 65536) / 255;
    
    uint8_t r, g, b;
    switch (sector % 6) {
        case 0: r = val; g = t; b = p; break;
        case 1: r = q; g = val; b = p; break;
        case 2: r = p; g = val; b = t; break;
        case 3: r = p; g = q; b = val; break;
        case 4: r = t; g = p; b = val; break;
        default: r = val; g = p; b = q; break;
    }
    return RgbwColor(0, r, g, b);
}

void demo1_Rainbow() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 1: RAINBOW MODE (RGB) - 80 LEDs, static rainbow across strip\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 80;
    RgbwColor leds[ledCount];
    
    for (uint16_t i = 0; i < ledCount; i++) {
        uint16_t hue = (uint32_t)i * 65536 / ledCount;
        leds[i] = hsvToRgb(hue, 255, 255);
    }
    
    printLedStrip(leds, ledCount, VizMode::RGB);
    printf("\n");
}

void demo2_WhitePulse() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 2: WHITE MODE (Grayscale) - 40 LEDs, sine wave brightness levels\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 40;
    
    for (int frame = 0; frame < 8; frame++) {
        float angle = frame * M_PI / 4.0f;
        uint8_t brightness = (uint8_t)(127.5f + 127.5f * sin(angle));
        
        RgbwColor leds[ledCount];
        for (uint16_t i = 0; i < ledCount; i++) {
            leds[i] = RgbwColor(brightness, 0, 0, 0);
        }
        
        printf("Frame %d (brightness=%3d): ", frame, brightness);
        printLedStrip(leds, ledCount, VizMode::Grayscale, 40);
    }
    printf("\n");
}

void demo3_RainbowWave() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 3: RAINBOW WAVE (RGB) - 80 LEDs, wave traveling left→right→left\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 80;
    const uint16_t waveWidth = 16;
    
    for (int frame = 0; frame < 12; frame++) {
        float progress = frame / 11.0f;
        float wavePos;
        
        if (progress < 0.5f) {
            wavePos = progress * 2.0f;
        } else {
            wavePos = (1.0f - progress) * 2.0f;
        }
        
        uint16_t centerPos = (uint16_t)(wavePos * (ledCount - 1));
        uint16_t hueOffset = (uint16_t)(progress * 16384);
        
        RgbwColor leds[ledCount];
        for (uint16_t i = 0; i < ledCount; i++) {
            uint16_t dist = abs((int)i - (int)centerPos);
            uint8_t intensity = 0;
            
            if (dist < waveWidth) {
                float t = (float)dist / waveWidth;
                intensity = (uint8_t)(255.0f * (1.0f - t) * (1.0f - t));
            }
            
            uint16_t hue = (hueOffset + (uint32_t)i * 65536 / ledCount) % 65536;
            RgbwColor base = hsvToRgb(hue, 255, 255);
            leds[i] = RgbwColor(0, 
                (uint16_t)base.R * intensity / 255,
                (uint16_t)base.G * intensity / 255,
                (uint16_t)base.B * intensity / 255);
        }
        
        printf("Frame %2d (center=%2d):\n  ", frame, centerPos);
        printLedStrip(leds, ledCount, VizMode::RGB, 40);
    }
    printf("\n");
}

void demo4_FlickerDetection() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 4: FLICKER DETECTION - Simulated brightness jump between frames\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 20;
    const uint8_t threshold = 15;
    
    printf("Threshold: %d (configurable)\n\n", threshold);
    
    RgbwColor prevLeds[ledCount];
    RgbwColor currLeds[ledCount];
    
    for (uint16_t i = 0; i < ledCount; i++) {
        prevLeds[i] = RgbwColor(100, 0, 0, 0);
        currLeds[i] = RgbwColor(100, 0, 0, 0);
    }
    
    printf("Frame 0 (uniform gray 100):\n  ");
    printLedStrip(prevLeds, ledCount, VizMode::Grayscale, 20);
    
    currLeds[5] = RgbwColor(150, 0, 0, 0);
    currLeds[10] = RgbwColor(50, 0, 0, 0);
    currLeds[15] = RgbwColor(120, 0, 0, 0);
    
    printf("Frame 1 (jumps at LEDs 5, 10, 15):\n  ");
    printLedStrip(currLeds, ledCount, VizMode::Grayscale, 20);
    
    printf("\nFlicker analysis:\n");
    for (uint16_t i = 0; i < ledCount; i++) {
        int16_t deltaW = abs((int)currLeds[i].W - (int)prevLeds[i].W);
        int16_t deltaR = abs((int)currLeds[i].R - (int)prevLeds[i].R);
        int16_t deltaG = abs((int)currLeds[i].G - (int)prevLeds[i].G);
        int16_t deltaB = abs((int)currLeds[i].B - (int)prevLeds[i].B);
        int16_t maxDelta = std::max({deltaW, deltaR, deltaG, deltaB});
        
        if (maxDelta > threshold) {
            printf("  \033[31m[!] LED %2d: delta=%3d (W:%d→%d)\033[0m\n", 
                   i, maxDelta, prevLeds[i].W, currLeds[i].W);
        }
    }
    printf("  -- 3 flickers detected --\n\n");
}

void demo5_BrightnessLevels() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 5: BRIGHTNESS LEVELS - RGB at 25%%, 50%%, 100%%, Grayscale at same\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 10;
    uint8_t brightnessLevels[] = {64, 128, 255};
    const char* labels[] = {"25%", "50%", "100%"};
    
    RgbwColor baseColorRgbw(255, 255, 128, 64);
    
    for (int lvl = 0; lvl < 3; lvl++) {
        uint8_t br = brightnessLevels[lvl];
        RgbwColor leds[ledCount];
        for (uint16_t i = 0; i < ledCount; i++) {
            leds[i] = RgbwColor(
                (uint16_t)baseColorRgbw.W * br / 255,
                (uint16_t)baseColorRgbw.R * br / 255,
                (uint16_t)baseColorRgbw.G * br / 255,
                (uint16_t)baseColorRgbw.B * br / 255
            );
        }
        
        printf("Brightness %4s RGB:       ", labels[lvl]);
        printLedStrip(leds, ledCount, VizMode::RGB, 10);
        
        printf("Brightness %4s Grayscale: ", labels[lvl]);
        printLedStrip(leds, ledCount, VizMode::Grayscale, 10);
    }
    printf("\n");
}

void demo6_Comparison() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("DEMO 6: SIDE-BY-SIDE - RGB vs Grayscale for same RGBW data\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    
    const uint16_t ledCount = 20;
    RgbwColor leds[ledCount];
    
    for (uint16_t i = 0; i < ledCount; i++) {
        if (i < 7) {
            leds[i] = RgbwColor(0, 255, 0, 0);
        } else if (i < 14) {
            leds[i] = RgbwColor(200, 0, 0, 0);
        } else {
            leds[i] = RgbwColor(100, 128, 128, 128);
        }
    }
    
    printf("Data: LEDs 0-6=Red, 7-13=White(200), 14-19=Mixed RGBW\n\n");
    
    printf("RGB mode (W blended into RGB):      ");
    printLedStrip(leds, ledCount, VizMode::RGB, 20);
    
    printf("Grayscale mode (W channel only):    ");
    printLedStrip(leds, ledCount, VizMode::Grayscale, 20);
    
    printf("\n");
}

int main() {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║     LED VISUALIZATION DEMO - 24-bit Color Terminal Output                ║\n");
    printf("║     Tests RGB mode, Grayscale mode, and flicker detection display        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    
    demo1_Rainbow();
    demo2_WhitePulse();
    demo3_RainbowWave();
    demo4_FlickerDetection();
    demo5_BrightnessLevels();
    demo6_Comparison();
    
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("END OF DEMO - All visualizations use \\033[38;2;R;G;Bm██\\033[0m sequences\n");
    printf("═══════════════════════════════════════════════════════════════════════════\n\n");
    
    return 0;
}

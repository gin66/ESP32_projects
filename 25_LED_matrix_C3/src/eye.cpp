#include "led_effects.h"
#include <math.h>

static float smoothstep(float t) {
    if (t < 0) return 0;
    if (t > 1) return 1;
    return t * t * (3.0f - 2.0f * t);
}

static float computeOpenness(unsigned long ms) {
    if (ms < 600) {
        return smoothstep((float)ms / 600.0f);
    }

    unsigned long t = (ms - 600) % 4000;

    if (t < 3600) return 1.0f;

    float phase = (float)(t - 3600) / 400.0f;
    if (phase < 0.3f) return 1.0f - smoothstep(phase / 0.3f);
    if (phase < 0.6f) return 0.0f;
    return smoothstep((phase - 0.6f) / 0.4f);
}

void calculateEye(LedColor* pixels, uint16_t width, uint16_t height, unsigned long elapsedMs) {
    float cx = 7.0f, cy = 7.0f;
    float lidRx = 7.0f;
    float lidRy = 3.0f;

    float openness = computeOpenness(elapsedMs);

    float t1 = elapsedMs * 0.0006f;
    float t2 = elapsedMs * 0.0009f;
    float pupilOx = sinf(t1) * 2.0f + sinf(t1 * 2.3f) * 0.5f;
    float pupilOy = cosf(t2) * 0.8f + cosf(t2 * 1.7f) * 0.3f;

    float irisR = 1.8f;
    float pupilR = 1.1f;

    float hlx = cx + pupilOx * 0.3f + 0.8f;
    float hly = cy + pupilOy * 0.3f - 1.0f;

    for (uint16_t py = 0; py < height; py++) {
        for (uint16_t px = 0; px < width; px++) {
            float sx = (float)(px + py);
            float sy = (float)((int)py - (int)px + 7);

            float nx = (sx - cx) / lidRx;
            float nx2 = nx * nx;

            if (nx2 > 1.0f) {
                pixels[py * width + px] = LedColor(0, 0, 0);
                continue;
            }

            float curve = 1.0f - nx2;
            float yUpper = cy - openness * lidRy * curve;
            float yLower = cy + openness * lidRy * curve;

            LedColor c(0, 0, 0);

            if (openness < 0.01f) {
                if (fabsf(sy - cy) < 0.6f) {
                    c = LedColor(100, 55, 65);
                }
            } else {
                bool inside = (sy > yUpper && sy < yLower);
                bool nearUpper = fabsf(sy - yUpper) < 0.8f;
                bool nearLower = fabsf(sy - yLower) < 0.8f;

                if (inside) {
                    float pdx = sx - cx - pupilOx;
                    float pdy = sy - cy - pupilOy;
                    float pd2 = pdx * pdx + pdy * pdy;

                    if (pd2 < pupilR * pupilR) {
                        c = LedColor(10, 10, 15);
                    } else if (pd2 < irisR * irisR) {
                        c = LedColor(40, 120, 240);
                    } else {
                        c = LedColor(210, 210, 220);
                    }

                    float hldx = sx - hlx;
                    float hldy = sy - hly;
                    if (hldx * hldx + hldy * hldy < 0.6f) {
                        c = LedColor(255, 255, 255);
                    }
                } else if (nearUpper || nearLower) {
                    c = LedColor(100, 55, 65);
                }
            }

            pixels[py * width + px] = c;
        }
    }
}

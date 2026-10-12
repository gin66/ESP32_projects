#include "analyzer.h"
#include <cstdio>
#include <algorithm>

Analyzer::Analyzer(uint8_t flickerThreshold) 
    : flickerThreshold(flickerThreshold) {}

int16_t Analyzer::calcMaxDelta(const RgbwColor& a, const RgbwColor& b) const {
    int16_t deltaW = abs((int)b.W - (int)a.W);
    int16_t deltaR = abs((int)b.R - (int)a.R);
    int16_t deltaG = abs((int)b.G - (int)a.G);
    int16_t deltaB = abs((int)b.B - (int)a.B);
    return std::max({deltaW, deltaR, deltaG, deltaB});
}

std::vector<FlickerEvent> Analyzer::detectFlicker(const FrameRecorder& recorder) const {
    std::vector<FlickerEvent> events;
    
    if (recorder.getFrameCount() < 2) return events;
    
    for (uint16_t frame = 1; frame < recorder.getFrameCount(); frame++) {
        const RgbwColor* prev = recorder.getFrame(frame - 1);
        const RgbwColor* curr = recorder.getFrame(frame);
        
        for (uint16_t led = 0; led < recorder.getLedCount(); led++) {
            int16_t delta = calcMaxDelta(prev[led], curr[led]);
            
            if (delta > (int16_t)flickerThreshold) {
                FlickerEvent ev;
                ev.frame = frame;
                ev.led = led;
                ev.delta = delta;
                ev.prevW = prev[led].W;
                ev.prevR = prev[led].R;
                ev.prevG = prev[led].G;
                ev.prevB = prev[led].B;
                ev.currW = curr[led].W;
                ev.currR = curr[led].R;
                ev.currG = curr[led].G;
                ev.currB = curr[led].B;
                events.push_back(ev);
            }
        }
    }
    
    return events;
}

void Analyzer::printColorBlock(const RgbwColor& color, VizMode mode) const {
    if (mode == VizMode::RGB) {
        uint8_t r = std::min(255, (int)color.R + (int)color.W);
        uint8_t g = std::min(255, (int)color.G + (int)color.W);
        uint8_t b = std::min(255, (int)color.B + (int)color.W);
        printf("\033[38;2;%d;%d;%dm██\033[0m", r, g, b);
    } else {
        uint8_t gray = color.W;
        printf("\033[38;2;%d;%d;%dm██\033[0m", gray, gray, gray);
    }
}

void Analyzer::printFrame(const FrameRecorder& recorder, uint16_t frameIdx, VizMode mode, uint16_t ledsPerLine) const {
    const RgbwColor* frame = recorder.getFrame(frameIdx);
    if (!frame) return;
    
    for (uint16_t i = 0; i < recorder.getLedCount(); i++) {
        printColorBlock(frame[i], mode);
        if (ledsPerLine > 0 && (i + 1) % ledsPerLine == 0 && i + 1 < recorder.getLedCount()) {
            printf("\n        ");
        }
    }
    printf("\n");
}

void Analyzer::printFlickerReport(const std::vector<FlickerEvent>& events) const {
    if (events.empty()) {
        printf("  No flicker detected (threshold=%d)\n", flickerThreshold);
        return;
    }
    
    printf("  \033[31mFlicker events (threshold=%d):\033[0m\n", flickerThreshold);
    for (const auto& ev : events) {
        printf("    [\033[31m!\033[0m] Frame %4d, LED %3d: delta=%3d", ev.frame, ev.led, ev.delta);
        if (ev.currW != ev.prevW || ev.currR != ev.prevR || ev.currG != ev.prevG || ev.currB != ev.prevB) {
            printf(" (");
            bool first = true;
            if (ev.currW != ev.prevW) {
                printf("W:%d→%d", ev.prevW, ev.currW);
                first = false;
            }
            if (ev.currR != ev.prevR) {
                printf("%sR:%d→%d", first ? "" : ", ", ev.prevR, ev.currR);
                first = false;
            }
            if (ev.currG != ev.prevG) {
                printf("%sG:%d→%d", first ? "" : ", ", ev.prevG, ev.currG);
                first = false;
            }
            if (ev.currB != ev.prevB) {
                printf("%sB:%d→%d", first ? "" : ", ", ev.prevB, ev.currB);
            }
            printf(")");
        }
        printf("\n");
    }
    printf("  \033[31m-- %zu flickers detected --\033[0m\n", events.size());
}

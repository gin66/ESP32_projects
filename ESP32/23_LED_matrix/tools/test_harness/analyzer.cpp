#include "analyzer.h"
#include <cstdio>
#include <algorithm>

Analyzer::Analyzer(uint8_t flickerThreshold) 
    : flickerThreshold(flickerThreshold) {}

int16_t Analyzer::calcMaxDelta(const LedColor& a, const LedColor& b) const {
    int16_t deltaR = abs((int)b.R - (int)a.R);
    int16_t deltaG = abs((int)b.G - (int)a.G);
    int16_t deltaB = abs((int)b.B - (int)a.B);
    return std::max({deltaR, deltaG, deltaB});
}

std::vector<FlickerEvent> Analyzer::detectFlicker(const FrameRecorder& recorder) const {
    std::vector<FlickerEvent> events;
    
    if (recorder.getFrameCount() < 2) return events;
    
    for (uint16_t frame = 1; frame < recorder.getFrameCount(); frame++) {
        const LedColor* prev = recorder.getFrame(frame - 1);
        const LedColor* curr = recorder.getFrame(frame);
        
        for (uint16_t y = 0; y < recorder.getHeight(); y++) {
            for (uint16_t x = 0; x < recorder.getWidth(); x++) {
                uint32_t idx = y * recorder.getWidth() + x;
                int16_t delta = calcMaxDelta(prev[idx], curr[idx]);
                
                if (delta > (int16_t)flickerThreshold) {
                    FlickerEvent ev;
                    ev.frame = frame;
                    ev.x = x;
                    ev.y = y;
                    ev.delta = delta;
                    ev.prevR = prev[idx].R;
                    ev.prevG = prev[idx].G;
                    ev.prevB = prev[idx].B;
                    ev.currR = curr[idx].R;
                    ev.currG = curr[idx].G;
                    ev.currB = curr[idx].B;
                    events.push_back(ev);
                }
            }
        }
    }
    
    return events;
}

void Analyzer::printColorBlock(const LedColor& color) const {
    printf("\033[38;2;%d;%d;%dm██\033[0m", color.R, color.G, color.B);
}

void Analyzer::printFrame2D(const FrameRecorder& recorder, uint16_t frameIdx, int scale) const {
    const LedColor* frame = recorder.getFrame(frameIdx);
    if (!frame) return;
    
    printf("  Frame %04d:\n", frameIdx);
    
    for (uint16_t y = 0; y < recorder.getHeight(); y++) {
        printf("  ");
        for (int s = 0; s < scale; s++) {
            for (uint16_t x = 0; x < recorder.getWidth(); x++) {
                const LedColor& pixel = frame[y * recorder.getWidth() + x];
                for (int sx = 0; sx < scale; sx++) {
                    printColorBlock(pixel);
                }
            }
            printf("\n  ");
        }
    }
    printf("\n");
}

void Analyzer::printFlickerReport(const std::vector<FlickerEvent>& events) const {
    if (events.empty()) {
        printf("  No flicker detected (threshold=%d)\n", flickerThreshold);
        return;
    }
    
    printf("  \033[31mFlicker events (threshold=%d): %zu total\033[0m\n", flickerThreshold, events.size());
    
    size_t shown = 0;
    for (const auto& ev : events) {
        if (shown >= 20) {
            printf("  ... and %zu more events\n", events.size() - shown);
            break;
        }
        printf("    [\033[31m!\033[0m] F%04d (%3d,%2d): delta=%3d (R:%d→%d G:%d→%d B:%d→%d)\n",
               ev.frame, ev.x, ev.y, ev.delta,
               ev.prevR, ev.currR, ev.prevG, ev.currG, ev.prevB, ev.currB);
        shown++;
    }
}

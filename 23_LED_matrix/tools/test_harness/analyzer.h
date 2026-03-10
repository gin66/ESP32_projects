#pragma once

#include "recorder.h"
#include <cstdint>
#include <vector>

struct FlickerEvent {
    uint16_t frame;
    uint16_t x;
    uint16_t y;
    int16_t delta;
    uint8_t prevR, prevG, prevB;
    uint8_t currR, currG, currB;
};

class Analyzer {
public:
    Analyzer(uint8_t flickerThreshold = 15);
    
    std::vector<FlickerEvent> detectFlicker(const FrameRecorder& recorder) const;
    
    void printColorBlock(const LedColor& color) const;
    void printFrame2D(const FrameRecorder& recorder, uint16_t frameIdx, int scale = 1) const;
    void printFlickerReport(const std::vector<FlickerEvent>& events) const;
    
    void setFlickerThreshold(uint8_t threshold) { flickerThreshold = threshold; }
    uint8_t getFlickerThreshold() const { return flickerThreshold; }
    
private:
    uint8_t flickerThreshold;
    
    int16_t calcMaxDelta(const LedColor& a, const LedColor& b) const;
};

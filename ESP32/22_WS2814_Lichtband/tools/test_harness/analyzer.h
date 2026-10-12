#pragma once

#include "recorder.h"
#include <cstdint>
#include <vector>

struct FlickerEvent {
    uint16_t frame;
    uint16_t led;
    int16_t delta;
    uint8_t prevW, prevR, prevG, prevB;
    uint8_t currW, currR, currG, currB;
};

enum class VizMode {
    RGB,
    Grayscale
};

class Analyzer {
public:
    Analyzer(uint8_t flickerThreshold = 15);
    
    std::vector<FlickerEvent> detectFlicker(const FrameRecorder& recorder) const;
    
    void printColorBlock(const RgbwColor& color, VizMode mode) const;
    void printFrame(const FrameRecorder& recorder, uint16_t frameIdx, VizMode mode, uint16_t ledsPerLine = 40) const;
    void printFlickerReport(const std::vector<FlickerEvent>& events) const;
    
    void setFlickerThreshold(uint8_t threshold) { flickerThreshold = threshold; }
    uint8_t getFlickerThreshold() const { return flickerThreshold; }
    
private:
    uint8_t flickerThreshold;
    
    int16_t calcMaxDelta(const RgbwColor& a, const RgbwColor& b) const;
};

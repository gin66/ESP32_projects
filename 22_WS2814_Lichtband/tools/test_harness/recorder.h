#pragma once

#include "../../include/led_config.h"
#include <vector>
#include <cstdint>

class FrameRecorder {
public:
    FrameRecorder(uint16_t ledCount, uint16_t maxFrames = 10000);
    ~FrameRecorder();
    
    void recordFrame(const RgbwColor* leds);
    void clear();
    
    uint16_t getFrameCount() const { return frameCount; }
    uint16_t getLedCount() const { return ledCount; }
    
    const RgbwColor* getFrame(uint16_t frameIdx) const;
    const RgbwColor* getLed(uint16_t frameIdx, uint16_t ledIdx) const;
    
private:
    uint16_t ledCount;
    uint16_t maxFrames;
    uint16_t frameCount;
    RgbwColor* frames;  // [maxFrames][ledCount]
};

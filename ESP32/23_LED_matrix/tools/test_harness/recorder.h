#pragma once

#include "../../include/led_config.h"
#include <vector>
#include <cstdint>

class FrameRecorder {
public:
    FrameRecorder(uint16_t width, uint16_t height, uint16_t maxFrames = 1000);
    ~FrameRecorder();
    
    void recordFrame(const LedColor* pixels);
    void clear();
    
    uint16_t getFrameCount() const { return frameCount; }
    uint16_t getWidth() const { return width; }
    uint16_t getHeight() const { return height; }
    uint32_t getPixelCount() const { return (uint32_t)width * height; }
    
    const LedColor* getFrame(uint16_t frameIdx) const;
    const LedColor* getPixel(uint16_t frameIdx, uint16_t x, uint16_t y) const;
    
private:
    uint16_t width;
    uint16_t height;
    uint16_t maxFrames;
    uint16_t frameCount;
    LedColor* frames;
};

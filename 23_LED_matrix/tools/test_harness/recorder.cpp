#include "recorder.h"
#include <cstring>

FrameRecorder::FrameRecorder(uint16_t width, uint16_t height, uint16_t maxFrames)
    : width(width), height(height), maxFrames(maxFrames), frameCount(0) {
    frames = new LedColor[(uint32_t)maxFrames * width * height];
}

FrameRecorder::~FrameRecorder() {
    delete[] frames;
}

void FrameRecorder::recordFrame(const LedColor* pixels) {
    if (frameCount >= maxFrames) return;
    memcpy(&frames[(uint32_t)frameCount * width * height], pixels, (uint32_t)width * height * sizeof(LedColor));
    frameCount++;
}

void FrameRecorder::clear() {
    frameCount = 0;
}

const LedColor* FrameRecorder::getFrame(uint16_t frameIdx) const {
    if (frameIdx >= frameCount) return nullptr;
    return &frames[(uint32_t)frameIdx * width * height];
}

const LedColor* FrameRecorder::getPixel(uint16_t frameIdx, uint16_t x, uint16_t y) const {
    if (frameIdx >= frameCount || x >= width || y >= height) return nullptr;
    return &frames[(uint32_t)frameIdx * width * height + y * width + x];
}

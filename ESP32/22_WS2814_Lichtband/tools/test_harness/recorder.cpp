#include "recorder.h"
#include <cstring>

FrameRecorder::FrameRecorder(uint16_t ledCount, uint16_t maxFrames)
    : ledCount(ledCount), maxFrames(maxFrames), frameCount(0) {
    frames = new RgbwColor[(uint32_t)maxFrames * ledCount];
}

FrameRecorder::~FrameRecorder() {
    delete[] frames;
}

void FrameRecorder::recordFrame(const RgbwColor* leds) {
    if (frameCount >= maxFrames) return;
    memcpy(&frames[(uint32_t)frameCount * ledCount], leds, ledCount * sizeof(RgbwColor));
    frameCount++;
}

void FrameRecorder::clear() {
    frameCount = 0;
}

const RgbwColor* FrameRecorder::getFrame(uint16_t frameIdx) const {
    if (frameIdx >= frameCount) return nullptr;
    return &frames[(uint32_t)frameIdx * ledCount];
}

const RgbwColor* FrameRecorder::getLed(uint16_t frameIdx, uint16_t ledIdx) const {
    if (frameIdx >= frameCount || ledIdx >= ledCount) return nullptr;
    return &frames[(uint32_t)frameIdx * ledCount + ledIdx];
}

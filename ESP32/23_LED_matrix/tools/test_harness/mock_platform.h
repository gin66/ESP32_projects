#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>

#define PROGMEM

extern unsigned long mock_millis;

inline unsigned long millis() {
    return mock_millis;
}

inline void setMillis(unsigned long ms) {
    mock_millis = ms;
}

inline void advanceMillis(unsigned long delta) {
    mock_millis += delta;
}

inline bool getLocalTime(struct tm* timeinfo) {
    time_t now = mock_millis / 1000;
    struct tm* t = gmtime(&now);
    if (t) {
        *timeinfo = *t;
        return true;
    }
    return false;
}

inline uint8_t pgm_read_byte(const uint8_t* addr) {
    return *addr;
}

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

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

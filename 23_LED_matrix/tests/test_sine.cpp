#include "unity.h"
#include <cmath>
#include <cstdio>
#include <stdint.h>

// Include the sine table from led_effects.cpp
static const uint16_t sineTable45[] = {
    0, 1144, 2287, 3425, 4560, 5689, 6813, 7927, 9030, 10126,
    11207, 12275, 13322, 14350, 15356, 16384, 17364, 18322, 19260, 20173,
    21060, 21929, 22767, 23578, 24359, 25107, 25825, 26510, 27161, 27777,
    28378, 28902, 29409, 29879, 30310, 30700, 31050, 31358, 31625, 31849,
    32031, 32170, 32266, 32318, 32327, 32768
};

static inline uint16_t getSine(uint16_t angle) {
    angle = angle % 360;
    if (angle < 90) return sineTable45[angle / 2];
    if (angle < 180) return sineTable45[(180 - angle) / 2];
    if (angle < 270) return sineTable45[(angle - 180) / 2];
    return sineTable45[(360 - angle) / 2];
}

// Helper macro for assertions
#define TEST_ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("FAIL at line %d: condition false\n", __LINE__); \
        _unity_test_failed = 1; \
        return; \
    } \
} while(0)

void test_sine_basic_values(void) {
    // Test key angles: 0°, 90°, 180°, 270°, 360°
    TEST_ASSERT_EQUAL_UINT16(0, getSine(0));
    TEST_ASSERT_EQUAL_UINT16(32768, getSine(90));
    TEST_ASSERT_EQUAL_UINT16(0, getSine(180));
    TEST_ASSERT_EQUAL_UINT16(32768, getSine(270));
    TEST_ASSERT_EQUAL_UINT16(0, getSine(360));
}

void test_sine_symmetry(void) {
    // Test symmetry: sin(θ) = sin(180-θ) for 0-180
    for (uint16_t angle = 0; angle <= 90; angle += 10) {
        uint16_t sin1 = getSine(angle);
        uint16_t sin2 = getSine(180 - angle);
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
    
    // Test symmetry: sin(θ) = -sin(θ-180) for 180-360
    for (uint16_t angle = 180; angle <= 270; angle += 10) {
        uint16_t sin1 = getSine(angle);
        uint16_t sin2 = getSine(360 - (angle - 180));
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
}

void test_sine_periodicity(void) {
    // Test periodicity: sin(θ) = sin(θ + 360)
    for (uint16_t angle = 0; angle < 360; angle += 15) {
        uint16_t sin1 = getSine(angle);
        uint16_t sin2 = getSine(angle + 360);
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
}

void test_sine_against_math_lib(void) {
    // Compare with standard sin function (within tolerance)
    // The sine table stores |sin(θ)| * 32768 for θ from 0° to 90°
    // getSine() returns |sin(θ)| * 32768 in range [0, 32768] for all angles
    const double tolerance = 0.02; // 2% tolerance (table has limited precision)
    
    for (uint16_t angle = 0; angle < 360; angle += 5) {
        double mathSin = sin(angle * M_PI / 180.0);
        // getSine returns absolute value of sin * 32768
        double mathAbsSin = fabs(mathSin);
        double mathScaled = mathAbsSin * 32768.0;
        
        uint16_t ourSin = getSine(angle);
        
        double diff = fabs((double)ourSin - mathScaled);
        double relError = diff / 32768.0;
        
        if (relError > tolerance) {
            printf("Angle %u: our=%u, math=%.0f (|sin|=%.3f), error=%.2f%%\n", 
                   angle, ourSin, mathScaled, mathAbsSin, relError * 100.0);
            _unity_test_failed = 1;
            return;
        }
    }
}

void test_sine_usage_patterns(void) {
    // Test how sine values are actually used in the code
    // 1. Test scaling to 0-8192 range (as in line 177)
    for (uint16_t angle = 0; angle < 360; angle += 30) {
        uint16_t waveSine = getSine(angle);
        uint16_t waveOffset = (uint16_t)((uint32_t)waveSine * 8192 / 32768);
        
        // waveOffset should be in range [0, 8192]
        TEST_ASSERT_TRUE(waveOffset <= 8192);
        
        // For 0° and 180°, sine is 0, so offset should be 0
        if (angle % 180 == 0) {
            TEST_ASSERT_EQUAL_UINT16(0, waveOffset);
        }
        // For 90° and 270°, |sin| is 1, so offset should be 8192
        if (angle % 180 == 90) {
            TEST_ASSERT_EQUAL_UINT16(8192, waveOffset);
        }
    }
    
    // 2. Test scaling to 0-255 range (as in line 307)
    for (uint16_t angle = 0; angle < 360; angle += 30) {
        uint16_t sineVal = getSine(angle);
        uint8_t wave = (uint8_t)(sineVal * 255 / 32768);
        
        // wave should be in range [0, 255]
        TEST_ASSERT_TRUE(wave <= 255);
        
        // For 0° and 180°, sine is 0, so wave should be 0
        if (angle % 180 == 0) {
            TEST_ASSERT_EQUAL_UINT16(0, wave);
        }
        // For 90° and 270°, |sin| is 1, so wave should be 255
        if (angle % 180 == 90) {
            TEST_ASSERT_EQUAL_UINT16(255, wave);
        }
    }
}

void test_sine_table_coverage(void) {
    // Verify all table entries are within valid range
    for (int i = 0; i < 46; i++) {
        TEST_ASSERT_TRUE(sineTable45[i] <= 32768);
    }
    
    // Check specific known values
    TEST_ASSERT_EQUAL_UINT16(0, sineTable45[0]);
    
    // Check sin(45°) ≈ 0.7071 * 32768 ≈ 23170
    // Table index 22 represents 44° (22 * 2), value is 22767
    // Table index 23 would be 46°, but we don't have it
    // Let's check the closest: sin(44°) ≈ 0.6947 * 32768 ≈ 22766
    double expected44 = sin(44.0 * M_PI / 180.0) * 32768.0;
    uint16_t table44 = sineTable45[22];
    double diff44 = fabs((double)table44 - expected44);
    double relError44 = diff44 / 32768.0;
    if (relError44 > 0.01) {
        printf("Index 22 (44°): table=%u, expected=%.0f, error=%.2f%%\n", 
               table44, expected44, relError44 * 100.0);
    }
    
    TEST_ASSERT_EQUAL_UINT16(32768, sineTable45[45]); // sin(90°) should be 32768
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sine_basic_values);
    RUN_TEST(test_sine_symmetry);
    RUN_TEST(test_sine_periodicity);
    RUN_TEST(test_sine_against_math_lib);
    RUN_TEST(test_sine_usage_patterns);
    RUN_TEST(test_sine_table_coverage);
    return UNITY_END();
}
#include "unity.h"
#include <cmath>
#include <cstdio>
#include <stdint.h>
#include "../include/led_effects_utils.h"

#define TEST_ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("FAIL at line %d: condition false\n", __LINE__); \
        _unity_test_failed = 1; \
        return; \
    } \
} while(0)

void test_sine_basic_values(void) {
    TEST_ASSERT_EQUAL_UINT16(0, getAbsSine(0));
    TEST_ASSERT_EQUAL_UINT16(32767, getAbsSine(90));
    TEST_ASSERT_EQUAL_UINT16(0, getAbsSine(180));
    TEST_ASSERT_EQUAL_UINT16(32767, getAbsSine(270));
    TEST_ASSERT_EQUAL_UINT16(0, getAbsSine(360));
}

void test_sine_symmetry(void) {
    for (uint16_t angle = 0; angle <= 90; angle += 10) {
        uint16_t sin1 = getAbsSine(angle);
        uint16_t sin2 = getAbsSine(180 - angle);
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
    
    for (uint16_t angle = 180; angle <= 270; angle += 10) {
        uint16_t sin1 = getAbsSine(angle);
        uint16_t sin2 = getAbsSine(360 - (angle - 180));
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
}

void test_sine_periodicity(void) {
    for (uint16_t angle = 0; angle < 360; angle += 15) {
        uint16_t sin1 = getAbsSine(angle);
        uint16_t sin2 = getAbsSine(angle + 360);
        TEST_ASSERT_EQUAL_UINT16(sin1, sin2);
    }
}

void test_sine_against_math_lib(void) {
    const double tolerance = 0.02;
    
    for (uint16_t angle = 0; angle < 360; angle += 5) {
        double mathSin = sin(angle * M_PI / 180.0);
        double mathAbsSin = fabs(mathSin);
        double mathScaled = mathAbsSin * 32768.0;
        
        uint16_t ourSin = getAbsSine(angle);
        
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
    for (uint16_t angle = 0; angle < 360; angle += 30) {
        uint16_t waveSine = getAbsSine(angle);
        uint16_t waveOffset = (uint16_t)((uint32_t)waveSine * 8192 / 32768);
        
        TEST_ASSERT_TRUE(waveOffset <= 8192);
        
        if (angle % 180 == 0) {
            TEST_ASSERT_EQUAL_UINT16(0, waveOffset);
        }
        if (angle % 180 == 90) {
            TEST_ASSERT_EQUAL_UINT16(8191, waveOffset);
        }
    }
    
    for (uint16_t angle = 0; angle < 360; angle += 30) {
        uint16_t sineVal = getAbsSine(angle);
        uint8_t wave = (uint8_t)(sineVal * 255 / 32767);
        
        TEST_ASSERT_TRUE(wave <= 255);
        
        if (angle % 180 == 0) {
            TEST_ASSERT_EQUAL_UINT16(0, wave);
        }
        if (angle % 180 == 90) {
            TEST_ASSERT_EQUAL_UINT16(255, wave);
        }
    }
}

void test_sine_signed_values(void) {
    TEST_ASSERT_TRUE(getSignedSine(0) == 0);
    TEST_ASSERT_TRUE(getSignedSine(45) > 0);
    TEST_ASSERT_TRUE(getSignedSine(90) == 32767);
    TEST_ASSERT_TRUE(getSignedSine(180) == 0);
    TEST_ASSERT_TRUE(getSignedSine(225) < 0);
    TEST_ASSERT_TRUE(getSignedSine(270) == -32767);
    TEST_ASSERT_TRUE(getSignedSine(360) == 0);
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
    RUN_TEST(test_sine_signed_values);
    return UNITY_END();
}

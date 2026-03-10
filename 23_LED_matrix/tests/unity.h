// Minimal Unity test framework - only what we need
#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>
#include <stdint.h>

static int _unity_failures = 0;
static int _unity_test_failed = 0;

#define UNITY_BEGIN() (_unity_failures = 0)

#define UNITY_END() \
    (_unity_failures > 0 ? (printf("Tests failed: %d\n", _unity_failures), 1) : (printf("All tests passed!\n"), 0))

#define RUN_TEST(test) do { \
    _unity_test_failed = 0; \
    printf("Running " #test "... "); \
    test(); \
    if (_unity_test_failed) { \
        _unity_failures++; \
    } else { \
        printf("OK\n"); \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_UINT16(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAIL at line %d: expected %u, got %u\n", __LINE__, (uint16_t)(expected), (uint16_t)(actual)); \
        _unity_test_failed = 1; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) TEST_ASSERT_EQUAL_UINT16(expected, actual)

#endif

#include "unity.h"
#include "../include/led_effects.h"

#define WIDTH 32
#define HEIGHT 32

// Reference: AVR IDX(x,y) = (x%2==0 ? y : (7-y)) + x*8
// Panel 0 (even): col = 31-x, so visual x=0 -> col=31, x=31 -> col=0

void test_panel1_corner(void) {
    // x=31 -> col=0 (even): base=0, idx=0+y
    TEST_ASSERT_EQUAL_UINT16(0, xyToIndex(31, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(7, xyToIndex(31, 7, WIDTH, HEIGHT));
    // x=0 -> col=31 (odd): base=248, idx=248+(7-y)
    TEST_ASSERT_EQUAL_UINT16(255, xyToIndex(0, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(248, xyToIndex(0, 7, WIDTH, HEIGHT));
}

void test_panel1_sequence(void) {
    // x=0 -> col=31 (odd): base=248, idx=248+(7-y)
    TEST_ASSERT_EQUAL_UINT16(255, xyToIndex(0, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(254, xyToIndex(0, 1, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(253, xyToIndex(0, 2, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(252, xyToIndex(0, 3, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(251, xyToIndex(0, 4, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(250, xyToIndex(0, 5, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(249, xyToIndex(0, 6, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(248, xyToIndex(0, 7, WIDTH, HEIGHT));

    // x=1 -> col=30 (even): base=240, idx=240+y
    TEST_ASSERT_EQUAL_UINT16(240, xyToIndex(1, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(241, xyToIndex(1, 1, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(242, xyToIndex(1, 2, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(243, xyToIndex(1, 3, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(244, xyToIndex(1, 4, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(245, xyToIndex(1, 5, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(246, xyToIndex(1, 6, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(247, xyToIndex(1, 7, WIDTH, HEIGHT));

    // x=2 -> col=29 (odd): base=232, idx=232+(7-y)
    TEST_ASSERT_EQUAL_UINT16(239, xyToIndex(2, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(238, xyToIndex(2, 1, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(237, xyToIndex(2, 2, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(236, xyToIndex(2, 3, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(235, xyToIndex(2, 4, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(234, xyToIndex(2, 5, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(233, xyToIndex(2, 6, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(232, xyToIndex(2, 7, WIDTH, HEIGHT));

    // x=3 -> col=28 (even): base=224, idx=224+y
    TEST_ASSERT_EQUAL_UINT16(224, xyToIndex(3, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(225, xyToIndex(3, 1, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(226, xyToIndex(3, 2, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(227, xyToIndex(3, 3, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(228, xyToIndex(3, 4, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(229, xyToIndex(3, 5, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(230, xyToIndex(3, 6, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(231, xyToIndex(3, 7, WIDTH, HEIGHT));

    // x=31 -> col=0 (even): base=0, idx=0+y
    TEST_ASSERT_EQUAL_UINT16(0, xyToIndex(31, 0, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(1, xyToIndex(31, 1, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(2, xyToIndex(31, 2, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(3, xyToIndex(31, 3, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(4, xyToIndex(31, 4, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(5, xyToIndex(31, 5, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(6, xyToIndex(31, 6, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(7, xyToIndex(31, 7, WIDTH, HEIGHT));
}

// Panel 2 (y=8..15, panelIndex=1, rotated 180°: col=x)
void test_panel2_corner(void) {
    // x=0 -> col=0 (even): base=0, idx=256+y
    TEST_ASSERT_EQUAL_UINT16(256, xyToIndex(0, 8, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(263, xyToIndex(0, 15, WIDTH, HEIGHT));
    // x=31 -> col=31 (odd): base=248, idx=256+248+(7-localY)
    TEST_ASSERT_EQUAL_UINT16(511, xyToIndex(31, 8, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(504, xyToIndex(31, 15, WIDTH, HEIGHT));
}

// Panel 3 (y=16..23, panelIndex=2, not rotated: col=31-x)
void test_panel3_corner(void) {
    // x=0 -> col=31 (odd): base=248, idx=512+248+(7-localY)
    TEST_ASSERT_EQUAL_UINT16(512 + 255, xyToIndex(0, 16, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(512 + 248, xyToIndex(0, 23, WIDTH, HEIGHT));
    // x=31 -> col=0 (even): base=0, idx=512+localY
    TEST_ASSERT_EQUAL_UINT16(512, xyToIndex(31, 16, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(512 + 7, xyToIndex(31, 23, WIDTH, HEIGHT));
}

// Panel 4 (y=24..31, panelIndex=3, rotated 180°: col=x)
void test_panel4_corner(void) {
    // x=0 -> col=0 (even): base=0, idx=768+localY
    TEST_ASSERT_EQUAL_UINT16(768, xyToIndex(0, 24, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(768 + 7, xyToIndex(0, 31, WIDTH, HEIGHT));
    // x=31 -> col=31 (odd): base=248, idx=768+248+(7-localY)
    TEST_ASSERT_EQUAL_UINT16(768 + 255, xyToIndex(31, 24, WIDTH, HEIGHT));
    TEST_ASSERT_EQUAL_UINT16(768 + 248, xyToIndex(31, 31, WIDTH, HEIGHT));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_panel1_corner);
    RUN_TEST(test_panel1_sequence);
    RUN_TEST(test_panel2_corner);
    RUN_TEST(test_panel3_corner);
    RUN_TEST(test_panel4_corner);
    return UNITY_END();
}

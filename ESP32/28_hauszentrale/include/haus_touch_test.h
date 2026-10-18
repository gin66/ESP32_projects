/**
 * @file haus_touch_test.h
 * @brief Touch calibration test: 5 rounds x 4 cross points, then statistics.
 *
 * Started from the web UI (`/touch/test`). The overlay is owned by the device
 * only; the host harness gets no-op stubs so `haus_ui.c` can still reference
 * the request function.
 */

#ifndef HAUS_TOUCH_TEST_H
#define HAUS_TOUCH_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAUS_HOST

static inline void haus_touch_test_request(void) {}
static inline void haus_touch_test_poll(void) {}
static inline bool haus_touch_test_active(void) { return false; }
static inline const char* haus_touch_test_last_stats(void) { return ""; }

#else

/* Ask the UI loop to open the calibration overlay. */
void haus_touch_test_request(void);

/* Called from loop(). Drives the state machine and the overlay. */
void haus_touch_test_poll(void);

/* True while the overlay is on screen. */
bool haus_touch_test_active(void);

/* Last result as a one-line JSON object, or an empty string. */
const char* haus_touch_test_last_stats(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* HAUS_TOUCH_TEST_H */

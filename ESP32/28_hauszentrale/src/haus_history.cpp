/**
 * @file haus_history.cpp
 * @brief L1/L2 store and the independent SPIFFS hourly ring.
 *
 * L1/L2 are allocated once from PSRAM and never reallocated. The SPIFFS ring
 * is a fixed 12-month file indexed by epoch hour; appending an hour is one
 * seek plus 88 bytes, with no metadata write.
 */

#include "haus_history.h"

#ifdef HAUS_HOST

static gui_sample_t s_l1[HAUS_L1_SAMPLES];
static RollupColumn s_l2[HAUS_L2_COLUMNS];

bool haus_history_init(void) { return true; }
gui_sample_t *haus_history_l1(void) { return s_l1; }
RollupColumn *haus_history_l2(void) { return s_l2; }
bool haus_history_ring_append(const RollupColumn *col) {
    (void)col;
    return false;
}
size_t haus_history_ring_load(RollupColumn *out, size_t max_columns) {
    (void)out;
    (void)max_columns;
    return 0;
}

#else  /* device */

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>

#include "tpl_debug_log.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
} RingHeader;

#define RING_MAGIC 0x484E4752u  /* "RNGH" */
#define RING_VERSION 1

static gui_sample_t *s_l1 = NULL;
static RollupColumn *s_l2 = NULL;
static bool s_ring_ready = false;

static size_t ring_slot(uint32_t epoch_hour) {
    return (size_t)(epoch_hour % HAUS_RING_HOURS);
}

static bool ring_open_or_create(void) {
    static const size_t need = sizeof(RingHeader) + HAUS_RING_BYTES;
    File f = SPIFFS.open(HAUS_RING_PATH, "r");
    if (f) {
        RingHeader h;
        const bool size_ok = (f.size() == need);
        const bool read_ok = (f.readBytes((char *)&h, sizeof(h)) == sizeof(h));
        f.close();
        if (size_ok && read_ok && h.magic == RING_MAGIC &&
            h.version == RING_VERSION) {
            return true;
        }
        TPL_LOGW("history", "ring header/size mismatch; rebuilding");
    }

    f = SPIFFS.open(HAUS_RING_PATH, "w");
    if (!f) {
        TPL_LOGE("history", "cannot create %s", HAUS_RING_PATH);
        return false;
    }

    RingHeader h = {.magic = RING_MAGIC, .version = RING_VERSION, .reserved = 0};
    f.write((const uint8_t *)&h, sizeof(h));

    /* SPIFFS cannot seek past EOF, so extend with zero chunks. This runs once,
     * when the ring file is first created. */
    uint8_t zero[256];
    memset(zero, 0, sizeof(zero));
    size_t remaining = HAUS_RING_BYTES;
    while (remaining > 0) {
        const size_t chunk = remaining < sizeof(zero) ? remaining : sizeof(zero);
        if (f.write(zero, chunk) != chunk) {
            TPL_LOGE("history", "ring extend failed (%u bytes left)",
                     (unsigned)remaining);
            f.close();
            SPIFFS.remove(HAUS_RING_PATH);
            return false;
        }
        remaining -= chunk;
    }
    f.close();

    TPL_LOGI("history", "ring created: %u hours, %u bytes",
             (unsigned)HAUS_RING_HOURS, (unsigned)HAUS_RING_BYTES);
    return true;
}

bool haus_history_init(void) {
    if (!s_l1) {
        s_l1 = (gui_sample_t *)heap_caps_malloc(
            HAUS_L1_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_l2) {
        s_l2 = (RollupColumn *)heap_caps_malloc(
            HAUS_L2_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_l1 || !s_l2) {
        TPL_LOGE("history", "L1/L2 PSRAM allocation failed (L1=%p L2=%p)",
                 (void *)s_l1, (void *)s_l2);
        return false;
    }

    s_ring_ready = ring_open_or_create();
    if (!s_ring_ready) {
        TPL_LOGE("history", "SPIFFS hourly ring unavailable");
    } else {
        TPL_LOGI("history", "L1=%u B L2=%u B at %p/%p, ring ready",
                 (unsigned)HAUS_L1_BYTES, (unsigned)HAUS_L2_BYTES,
                 (void *)s_l1, (void *)s_l2);
    }
    return s_ring_ready;
}

gui_sample_t *haus_history_l1(void) { return s_l1; }
RollupColumn *haus_history_l2(void) { return s_l2; }

bool haus_history_ring_append(const RollupColumn *col) {
    if (!s_ring_ready || !col || col->t == 0) return false;

    const size_t slot = ring_slot((uint32_t)(col->t / 60));
    const size_t off = sizeof(RingHeader) + slot * sizeof(RollupColumn);

    File f = SPIFFS.open(HAUS_RING_PATH, "r+");
    if (!f) return false;
    const bool ok = f.seek(off) &&
                    (f.write((const uint8_t *)col, sizeof(*col)) ==
                     sizeof(*col));
    f.close();
    return ok;
}

size_t haus_history_ring_load(RollupColumn *out, size_t max_columns) {
    if (!s_ring_ready || !out || max_columns == 0) return 0;

    const time_t now = time(nullptr);
    if (now < 24 * 3600) return 0;  /* no valid wall clock yet */

    const uint32_t now_hour = (uint32_t)(now / 3600);
    if (max_columns > HAUS_RING_HOURS) max_columns = HAUS_RING_HOURS;

    const uint32_t first_hour = now_hour - (uint32_t)max_columns + 1;

    File f = SPIFFS.open(HAUS_RING_PATH, "r");
    if (!f) return 0;

    size_t written = 0;
    for (size_t i = 0; i < max_columns; i++) {
        const size_t slot = ring_slot(first_hour + (uint32_t)i);
        const size_t off = sizeof(RingHeader) + slot * sizeof(RollupColumn);
        RollupColumn col;
        if (!f.seek(off) ||
            f.readBytes((char *)&col, sizeof(col)) != sizeof(col)) {
            break;
        }
        if (col.t == 0) continue;
        out[written++] = col;
    }
    f.close();
    return written;
}

#endif /* HAUS_HOST */

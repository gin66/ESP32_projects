/**
 * @file haus_history.cpp
 * @brief L1/L2 store and the independent SPIFFS hourly ring.
 *
 * L1/L2 are allocated once from PSRAM and never reallocated. The SPIFFS ring
 * is a fixed 12-month file indexed by epoch hour; appending an hour is one
 * seek plus 88 bytes, with no metadata write.
 */

#include "haus_history.h"

#include <string.h>

#ifdef HAUS_HOST

static gui_sample_t s_l1_storage[HAUS_L1_SAMPLES];
static RollupColumn s_l2_storage[HAUS_L2_COLUMNS];

#else  /* device */

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include "tpl_debug_log.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
} RingHeader;

#define RING_MAGIC 0x484E4752u  /* "RNGH" */
#define RING_VERSION 1

static bool s_ring_ready = false;

#endif /* HAUS_HOST */

static gui_sample_t *s_l1 = NULL;
static RollupColumn *s_l2 = NULL;
static uint32_t s_last_sample_minute = UINT32_MAX;
static uint32_t s_last_day = 0;

static size_t ring_slot(uint32_t epoch_hour) {
    return (size_t)(epoch_hour % HAUS_RING_HOURS);
}

static void add_series(RollupColumn *col, int series, int32_t value) {
    if (col->n[series] == 0) {
        col->mn[series] = value;
        col->mx[series] = value;
    } else {
        if (value < col->mn[series]) col->mn[series] = value;
        if (value > col->mx[series]) col->mx[series] = value;
    }
    col->sum[series] += value;
    col->n[series]++;
}

bool haus_history_sample(const HausState *state, uint32_t epoch_seconds) {
    if (!s_l1 || !s_l2 || !state) return false;
    if (epoch_seconds < 24 * 3600) return false;

    const uint32_t minute = epoch_seconds / 60;
    if (minute == s_last_sample_minute) return true;
    s_last_sample_minute = minute;

    const uint32_t day = epoch_seconds / 86400;
    if (s_last_day == 0) {
        s_last_day = day;
    } else if (day != s_last_day) {
        /* Local midnight: today's minute buffer starts fresh. */
        memset(s_l1, 0, HAUS_L1_BYTES);
        s_last_day = day;
    }

    const uint32_t minute_of_day = (epoch_seconds % 86400) / 60;
    gui_sample_t *s = &s_l1[minute_of_day];
    s->epoch = epoch_seconds - (epoch_seconds % 60);
    s->temp[0] = state->can.temp_by_fn[SENSOR_FN_CHAMBER];
    s->temp[1] = state->can.temp_by_fn[SENSOR_FN_WATER_IN];
    s->temp[2] = state->can.temp_by_fn[SENSOR_FN_WATER_OUT];
    s->temp[3] = HAUS_CAN_TEMP_EMPTY;
    s->duty_permille = state->can.pwm_permille;
    s->watts = state->can.watts;
    s->flags = state->can.water_modelled ? 0x01 : 0x00;

    const uint32_t hour_start_sec = epoch_seconds - (epoch_seconds % 3600);
    const uint32_t hour_start_min = hour_start_sec / 60;
    const size_t slot = (hour_start_sec / 3600) % HAUS_L2_COLUMNS;
    RollupColumn *col = &s_l2[slot];

    if (col->t != hour_start_min) {
        memset(col, 0, sizeof(*col));
        col->t = hour_start_min;
    }

    if (s->temp[0] != HAUS_CAN_TEMP_EMPTY)
        add_series(col, 0, s->temp[0]);
    if (s->temp[1] != HAUS_CAN_TEMP_EMPTY)
        add_series(col, 1, s->temp[1]);
    if (s->temp[2] != HAUS_CAN_TEMP_EMPTY)
        add_series(col, 2, s->temp[2]);
    if (state->climate.climate_flags & CLIMATE_VALID_AHT20) {
        add_series(col, 3, state->climate.living_c100);
        add_series(col, 4, (int32_t)state->climate.hum_c100);
    }
    if (state->climate.climate_flags & CLIMATE_VALID_BMP280)
        add_series(col, 5, (int32_t)state->climate.pressure_pa);

    /* The hour is complete at minute 59. */
    if ((epoch_seconds % 3600) >= 3540) {
#ifndef HAUS_HOST
        if (s_ring_ready) {
            if (haus_history_ring_append(col)) {
                TPL_LOGI("history", "hour appended: t=%u n[0]=%u\n",
                         (unsigned)col->t, (unsigned)col->n[0]);
            } else {
                TPL_LOGW("history", "hour append failed: t=%u",
                         (unsigned)col->t);
            }
        }
#endif
    }

    return true;
}

size_t haus_history_l1_count(void) {
    if (!s_l1) return 0;
    size_t n = 0;
    for (size_t i = 0; i < HAUS_L1_SAMPLES; i++) {
        if (s_l1[i].epoch != 0) n++;
    }
    return n;
}

void haus_history_flush_hour(uint32_t epoch_seconds) {
#ifndef HAUS_HOST
    if (!s_ring_ready || !s_l2) return;
    const uint32_t hour_start_sec = epoch_seconds - (epoch_seconds % 3600);
    const size_t slot = (hour_start_sec / 3600) % HAUS_L2_COLUMNS;
    if (s_l2[slot].t != 0) haus_history_ring_append(&s_l2[slot]);
#else
    (void)epoch_seconds;
#endif
}

#ifdef HAUS_HOST

bool haus_history_init(void) {
    s_l1 = s_l1_storage;
    s_l2 = s_l2_storage;
    return true;
}

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

void haus_history_start_recorder(void) {}

#else  /* device */

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

static void recorder_task(void *arg) {
    (void)arg;
    uint32_t last_minute = UINT32_MAX;

    for (;;) {
        const time_t now = time(nullptr);
        if (now > 1700000000) {
            const uint32_t minute = (uint32_t)(now / 60);
            if (minute != last_minute) {
                last_minute = minute;
                HausState st;
                gui_get_state(&st);
                if (haus_history_sample(&st, (uint32_t)now)) {
                    static bool logged_first = false;
                    if (!logged_first) {
                        logged_first = true;
                        TPL_LOGI("history", "first sample at epoch %u\n",
                                 (unsigned)now);
                    }
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void haus_history_start_recorder(void) {
    xTaskCreatePinnedToCore(recorder_task, "HistoryRecorder", 4096, NULL, 2,
                            NULL, 0);
}

#endif /* HAUS_HOST */

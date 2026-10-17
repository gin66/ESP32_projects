/**
 * @file haus_history.cpp
 * @brief L1/L2 store and the independent SPIFFS hourly ring.
 *
 * L1/L2 are allocated once from PSRAM and never reallocated. The SPIFFS ring
 * is a fixed 12-month file indexed by epoch hour; appending an hour is one
 * seek plus 88 bytes, with no metadata write.
 */

#include "haus_history.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef HAUS_HOST

#define TPL_LOGI(...) do {} while (0)
#define TPL_LOGW(...) do {} while (0)
#define TPL_LOGE(...) do {} while (0)

static gui_sample_t s_l1_storage[HAUS_L1_SAMPLES];
static RollupColumn s_l2_storage[HAUS_L2_COLUMNS];
static RollupColumn s_ring_storage[HAUS_RING_HOURS];
static RollupColumn s_test_rollup_data[HAUS_L2_COLUMNS];
static bool s_test_rollup_valid = false;
static uint32_t s_test_day_bytes = 0;
static uint32_t s_test_rollup_bytes = 0;
static bool s_test_fail_writes = false;

#else  /* device */

#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "haus_sd.h"
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
static uint32_t s_last_sample_epoch = 0;
static uint32_t s_last_day = 0;
static uint32_t s_sd_write_count = 0;
static char s_last_day_path[32] = "";
static uint8_t s_last_stale_mask = 0;

/* One minute without a refresh marks a source stale in the log. */
#define HAUS_STALE_AFTER_S 60

static bool source_stale(uint32_t now, uint32_t last_update) {
    if (last_update == 0) return true;
    if (now <= last_update) return false;  /* clock step back */
    return (now - last_update) >= HAUS_STALE_AFTER_S;
}

static size_t ring_slot(uint32_t epoch_hour) {
    return (size_t)(epoch_hour % HAUS_RING_HOURS);
}

static void l2_store(const RollupColumn *col) {
    if (!s_l2 || !col || col->t == 0) return;
    const size_t slot = (col->t / 60u) % HAUS_L2_COLUMNS;
    s_l2[slot] = *col;
}

/* Local calendar day, used for the midnight rollover. A DST change makes the
 * day 23 or 25 hours long; a UTC day number would split it in the wrong place. */
static uint32_t local_day_key(uint32_t epoch) {
    time_t t = (time_t)epoch;
    struct tm tm;
    if (!localtime_r(&t, &tm)) return epoch / 86400u;
    return ((uint32_t)(tm.tm_year + 1900) * 1000u) + (uint32_t)tm.tm_yday;
}

/* Day names come from the last sample's epoch, not from a separate
 * localtime() read, so an NTP step cannot split a day across two files (D34). */
static void format_day_path(uint32_t epoch, char *out, size_t n) {
    time_t t = (time_t)epoch;
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        snprintf(out, n, "/data_00000000.bin");
        return;
    }
    char date[16];
    strftime(date, sizeof(date), "%Y%m%d", &tm);
    snprintf(out, n, "/data_%s.bin", date);
}

static bool sd_write_day(uint32_t epoch) {
    format_day_path(epoch, s_last_day_path, sizeof(s_last_day_path));

#ifdef HAUS_HOST
    if (s_test_fail_writes) return false;
    s_test_day_bytes = HAUS_L1_BYTES;
    s_sd_write_count++;
    return true;
#else
    if (!haus_sd_lock(2000)) return false;
    File f = SD.open(s_last_day_path, FILE_WRITE);
    const bool ok =
        f && (f.write((const uint8_t *)s_l1, HAUS_L1_BYTES) == HAUS_L1_BYTES);
    if (f) f.close();
    haus_sd_unlock();

    if (ok) {
        s_sd_write_count++;
        TPL_LOGI("history", "day file written: %s (%u B)", s_last_day_path,
                 (unsigned)HAUS_L1_BYTES);
    } else {
        TPL_LOGE("history", "day file write failed: %s", s_last_day_path);
    }
    return ok;
#endif
}

static bool sd_write_rollup(uint32_t epoch) {
#ifdef HAUS_HOST
    if (s_test_fail_writes) return false;
    const uint32_t last_hour = epoch / 3600u;
    for (size_t i = 0; i < HAUS_L2_COLUMNS; i++) {
        const uint32_t h = last_hour - (uint32_t)(HAUS_L2_COLUMNS - 1) +
                           (uint32_t)i;
        s_test_rollup_data[i] = s_l2[h % HAUS_L2_COLUMNS];
    }
    s_test_rollup_valid = true;
    s_test_rollup_bytes = HAUS_L2_BYTES;
    s_sd_write_count++;
    return true;
#else
    const uint32_t last_hour = epoch / 3600;
    if (!haus_sd_lock(2000)) return false;

    File f = SD.open("/rollup.bin", FILE_WRITE);
    bool ok = (bool)f;
    if (ok) {
        RollupColumn buf[16];
        size_t idx = 0;
        for (size_t i = 0; i < HAUS_L2_COLUMNS; i++) {
            const uint32_t h = last_hour - (HAUS_L2_COLUMNS - 1) + (uint32_t)i;
            buf[idx++] = s_l2[h % HAUS_L2_COLUMNS];
            if (idx == sizeof(buf) / sizeof(buf[0])) {
                const size_t bytes = idx * sizeof(RollupColumn);
                if (f.write((const uint8_t *)buf, bytes) != bytes) {
                    ok = false;
                    break;
                }
                idx = 0;
            }
        }
        if (ok && idx != 0) {
            const size_t bytes = idx * sizeof(RollupColumn);
            if (f.write((const uint8_t *)buf, bytes) != bytes) ok = false;
        }
    }
    if (f) f.close();
    haus_sd_unlock();

    if (ok) {
        s_sd_write_count++;
        TPL_LOGI("history", "rollup written: %u columns", (unsigned)HAUS_L2_COLUMNS);
    } else {
        TPL_LOGE("history", "rollup write failed");
    }
    return ok;
#endif
}

static void write_daily_files(uint32_t epoch) {
    if (epoch == 0) return;
    if (!sd_write_day(epoch)) {
        TPL_LOGW("history", "day file not written");
    }
    if (!sd_write_rollup(epoch)) {
        TPL_LOGW("history", "rollup not written");
    }
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

    const uint32_t day = local_day_key(epoch_seconds);
    if (s_last_day == 0) {
        s_last_day = day;
    } else if (day != s_last_day) {
        /* Local midnight: write the finished day and the rollup, then start
         * today's minute buffer fresh. */
        write_daily_files(s_last_sample_epoch);
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
    s->flags = state->can.water_modelled ? SAMPLE_FLAG_MODELLED : 0x00;

    uint8_t stale = 0;
    const bool living_valid = (state->climate.climate_flags & CLIMATE_VALID_AHT20) != 0;
    const bool pressure_valid = (state->climate.climate_flags & CLIMATE_VALID_BMP280) != 0;

    if (s->temp[0] != HAUS_CAN_TEMP_EMPTY &&
        source_stale(epoch_seconds, state->can.last_update_epoch))
        stale |= SAMPLE_STALE_CHAMBER;
    if (s->temp[1] != HAUS_CAN_TEMP_EMPTY &&
        source_stale(epoch_seconds, state->can.last_update_epoch))
        stale |= SAMPLE_STALE_WATER_IN;
    if (s->temp[2] != HAUS_CAN_TEMP_EMPTY &&
        source_stale(epoch_seconds, state->can.last_update_epoch))
        stale |= SAMPLE_STALE_WATER_OUT;
    if (living_valid &&
        source_stale(epoch_seconds, state->climate.last_update_epoch))
        stale |= SAMPLE_STALE_LIVING | SAMPLE_STALE_HUMIDITY;
    if (pressure_valid &&
        source_stale(epoch_seconds, state->climate.last_update_epoch))
        stale |= SAMPLE_STALE_PRESSURE;
    if (state->can.valid &&
        source_stale(epoch_seconds, state->can.last_update_epoch))
        stale |= SAMPLE_STALE_POWER;

    s->stale_mask = stale;
    if (stale != 0) s->flags |= SAMPLE_FLAG_STALE;
    if (stale != s_last_stale_mask) {
        TPL_LOGW("history", "stale mask 0x%02x -> 0x%02x",
                 (unsigned)s_last_stale_mask, (unsigned)stale);
        s_last_stale_mask = stale;
    }

    const uint32_t hour_start_sec = epoch_seconds - (epoch_seconds % 3600);
    const uint32_t hour_start_min = hour_start_sec / 60;
    const size_t slot = (hour_start_sec / 3600) % HAUS_L2_COLUMNS;
    RollupColumn *col = &s_l2[slot];

    if (col->t != hour_start_min) {
        memset(col, 0, sizeof(*col));
        col->t = hour_start_min;
    }

    /* Stale minutes are recorded in L1 with their mask, but never folded into
     * the hourly aggregate. A completed hour with n < 60 is therefore visibly
     * partial rather than silently complete. */
    if (s->temp[0] != HAUS_CAN_TEMP_EMPTY &&
        !(stale & SAMPLE_STALE_CHAMBER))
        add_series(col, 0, s->temp[0]);
    if (s->temp[1] != HAUS_CAN_TEMP_EMPTY &&
        !(stale & SAMPLE_STALE_WATER_IN))
        add_series(col, 1, s->temp[1]);
    if (s->temp[2] != HAUS_CAN_TEMP_EMPTY &&
        !(stale & SAMPLE_STALE_WATER_OUT))
        add_series(col, 2, s->temp[2]);
    if (living_valid && !(stale & SAMPLE_STALE_LIVING)) {
        add_series(col, 3, state->climate.living_c100);
        add_series(col, 4, (int32_t)state->climate.hum_c100);
    }
    if (pressure_valid && !(stale & SAMPLE_STALE_PRESSURE))
        add_series(col, 5, (int32_t)state->climate.pressure_pa);

    /* The hour is complete at minute 59. */
    if ((epoch_seconds % 3600) >= 3540) {
#ifdef HAUS_HOST
        (void)haus_history_ring_append(col);
#else
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

    s_last_sample_epoch = epoch_seconds;
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

uint32_t haus_history_sd_write_count(void) { return s_sd_write_count; }

const char *haus_history_last_day_path(void) { return s_last_day_path; }

#ifdef HAUS_HOST

bool haus_history_init(void) {
    s_l1 = s_l1_storage;
    s_l2 = s_l2_storage;
    memset(s_l1, 0, HAUS_L1_BYTES);
    memset(s_l2, 0, HAUS_L2_BYTES);
    memset(s_ring_storage, 0, sizeof(s_ring_storage));
    s_test_day_bytes = 0;
    s_test_rollup_bytes = 0;
    s_last_sample_minute = UINT32_MAX;
    s_last_sample_epoch = 0;
    s_last_day = 0;
    s_last_stale_mask = 0;
    s_sd_write_count = 0;
    s_last_day_path[0] = '\0';
    return true;
}

gui_sample_t *haus_history_l1(void) { return s_l1; }
RollupColumn *haus_history_l2(void) { return s_l2; }

bool haus_history_ring_append(const RollupColumn *col) {
    if (!col || col->t == 0) return false;
    const size_t slot = ring_slot((uint32_t)(col->t / 60));
    s_ring_storage[slot] = *col;
    return true;
}

size_t haus_history_ring_load_at(RollupColumn *out, size_t max_columns,
                                 uint32_t now_epoch) {
    if (!out || max_columns == 0) return 0;
    if (now_epoch < 24 * 3600) return 0;
    if (max_columns > HAUS_RING_HOURS) max_columns = HAUS_RING_HOURS;

    const uint32_t now_hour = now_epoch / 3600;
    const uint32_t first_hour = now_hour - (uint32_t)max_columns + 1;

    size_t written = 0;
    for (size_t i = 0; i < max_columns; i++) {
        const size_t slot = ring_slot(first_hour + (uint32_t)i);
        if (s_ring_storage[slot].t == 0) continue;
        out[written++] = s_ring_storage[slot];
    }
    return written;
}

size_t haus_history_ring_load(RollupColumn *out, size_t max_columns) {
    return haus_history_ring_load_at(out, max_columns, (uint32_t)time(nullptr));
}

size_t haus_history_restore(uint32_t now_epoch) {
    if (s_test_rollup_valid) {
        for (size_t i = 0; i < HAUS_L2_COLUMNS; i++) {
            l2_store(&s_test_rollup_data[i]);
        }
        return HAUS_L2_COLUMNS;
    }

    if (now_epoch < 24 * 3600u) return 0;
    const uint32_t now_hour = now_epoch / 3600u;
    const uint32_t first_hour = now_hour - (uint32_t)(HAUS_L2_COLUMNS - 1);
    size_t restored = 0;
    for (uint32_t i = 0; i < (uint32_t)HAUS_L2_COLUMNS; i++) {
        const size_t slot = ring_slot(first_hour + i);
        if (s_ring_storage[slot].t == 0) continue;
        l2_store(&s_ring_storage[slot]);
        restored++;
    }
    return restored;
}

void haus_history_start_recorder(void) {}

uint32_t haus_history_test_day_bytes(void) { return s_test_day_bytes; }
uint32_t haus_history_test_rollup_bytes(void) { return s_test_rollup_bytes; }
void haus_history_test_fail_writes(bool fail) { s_test_fail_writes = fail; }
void haus_history_test_clear_files(void) {
    memset(s_test_rollup_data, 0, sizeof(s_test_rollup_data));
    s_test_rollup_valid = false;
    s_test_day_bytes = 0;
    s_test_rollup_bytes = 0;
    s_last_day_path[0] = '\0';
}

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

    memset(s_l1, 0, HAUS_L1_BYTES);
    memset(s_l2, 0, HAUS_L2_BYTES);
    s_last_sample_minute = UINT32_MAX;
    s_last_sample_epoch = 0;
    s_last_day = 0;
    s_last_stale_mask = 0;
    s_sd_write_count = 0;
    s_last_day_path[0] = '\0';

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

size_t haus_history_ring_load_at(RollupColumn *out, size_t max_columns,
                                 uint32_t now_epoch) {
    if (!s_ring_ready || !out || max_columns == 0) return 0;
    if (now_epoch < 24 * 3600) return 0;

    const uint32_t now_hour = now_epoch / 3600;
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

size_t haus_history_ring_load(RollupColumn *out, size_t max_columns) {
    return haus_history_ring_load_at(out, max_columns, (uint32_t)time(nullptr));
}

size_t haus_history_restore(uint32_t now_epoch) {
    size_t restored = 0;

    /* SD rollup first: it is a contiguous 744-column block and needs no clock. */
    if (haus_sd_is_mounted() && haus_sd_lock(2000)) {
        File f = SD.open("/rollup.bin", FILE_READ);
        if (f) {
            RollupColumn buf[16];
            for (;;) {
                const size_t got = f.read((uint8_t *)buf, sizeof(buf));
                if (got == 0) break;
                const size_t cols = got / sizeof(RollupColumn);
                for (size_t i = 0; i < cols; i++) {
                    if (buf[i].t != 0) {
                        l2_store(&buf[i]);
                        restored++;
                    }
                }
                if (cols == 0 || (got % sizeof(RollupColumn)) != 0) break;
            }
            f.close();
        }
        haus_sd_unlock();
    }

    if (restored > 0) {
        TPL_LOGI("history", "restored %u columns from SD rollup",
                 (unsigned)restored);
        return restored;
    }

    /* Card absent or rollup unreadable: the SPIFFS ring is the other book. */
    if (s_ring_ready && now_epoch >= 24 * 3600u) {
        File f = SPIFFS.open(HAUS_RING_PATH, "r");
        if (f) {
            const uint32_t now_hour = now_epoch / 3600u;
            const uint32_t first_hour =
                now_hour - (uint32_t)(HAUS_L2_COLUMNS - 1);
            for (uint32_t i = 0; i < (uint32_t)HAUS_L2_COLUMNS; i++) {
                const size_t slot = ring_slot(first_hour + i);
                const size_t off =
                    sizeof(RingHeader) + slot * sizeof(RollupColumn);
                RollupColumn col;
                if (!f.seek(off) ||
                    f.readBytes((char *)&col, sizeof(col)) != sizeof(col)) {
                    break;
                }
                if (col.t == 0) continue;
                l2_store(&col);
                restored++;
            }
            f.close();
        }
    }

    TPL_LOGI("history", "restored %u columns from SPIFFS ring",
             (unsigned)restored);
    return restored;
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

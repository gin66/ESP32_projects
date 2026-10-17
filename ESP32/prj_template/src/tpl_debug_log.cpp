#include "tpl_debug_log.h"
#include "tpl_webserver.h"

#ifdef USE_TPL_DEBUG_LOG

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Legacy fixed-line buffer (kept for compatibility, used only if the PSRAM
 *  ring could not be allocated)
 * ═══════════════════════════════════════════════════════════════════════════ */
/* Allocated only when the PSRAM ring could not be created, so the normal
 * path does not burn DEBUG_LOG_LINES * DEBUG_LOG_LINE_SIZE bytes of internal
 * RAM. */
static char *log_buf = NULL;
static int log_head = 0;
static int log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;

/* ═══════════════════════════════════════════════════════════════════════════
 *  PSRAM byte ring
 *
 *  s_head is the next write index, s_used is the number of valid bytes
 *  (0..DEBUG_RING_SIZE) and s_total is the absolute byte counter. Absolute
 *  counters make the per-client WebSocket cursors simple: a cursor is a value
 *  in [s_total - s_used, s_total], and any cursor below the floor has been
 *  overwritten and is reported as dropped.
 * ═══════════════════════════════════════════════════════════════════════════ */
static uint8_t *ring_buf = NULL;
static uint32_t ring_size = 0;   /* 0 = ring disabled (use legacy buffer) */
static uint32_t ring_head = 0;   /* next write index */
static uint32_t ring_used = 0;   /* valid bytes */
static uint32_t ring_total = 0;  /* total bytes ever written */
static SemaphoreHandle_t ring_mutex = NULL;

/* Store a record in the fixed-line legacy buffer. Used only when no byte ring
 * could be allocated, so /log still returns something on a board without
 * PSRAM. */
static void legacy_store(const char *rec, int len) {
    if (!log_mutex || !rec || len <= 0) return;
    if (!log_buf) {
        log_buf = (char *)heap_caps_malloc(
            (size_t)DEBUG_LOG_LINES * DEBUG_LOG_LINE_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!log_buf) return;
    }
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    int n = len < DEBUG_LOG_LINE_SIZE - 1 ? len : DEBUG_LOG_LINE_SIZE - 1;
    char *line = log_buf + log_head * DEBUG_LOG_LINE_SIZE;
    memcpy(line, rec, n);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        n--;
    }
    line[n] = '\0';
    log_head = (log_head + 1) % DEBUG_LOG_LINES;
    if (log_count < DEBUG_LOG_LINES) log_count++;
    xSemaphoreGive(log_mutex);
}

/* Serial mirror. Default comes from -DDEBUG_LOG_SERIAL. */
static volatile bool serial_enabled = DEBUG_LOG_SERIAL ? true : false;

/* Per-client WebSocket cursors. */
#define TPL_DEBUG_WS_CLIENTS 8
struct WsCursor {
    uint32_t seq;
    bool active;
};
static WsCursor ws_cursors[TPL_DEBUG_WS_CLIENTS];

/* ── ring primitives ─────────────────────────────────────────────────────── */

static inline uint32_t ring_floor(void) {
    return ring_total - ring_used;
}

/* Copy `len` bytes starting at absolute position `seq` into dst. The caller
 * must hold ring_mutex. */
static void ring_copy(uint8_t *dst, uint32_t seq, uint32_t len) {
    if (!ring_size) return;
    uint32_t idx = seq % ring_size;
    uint32_t first = ring_size - idx;
    if (first > len) first = len;
    memcpy(dst, ring_buf + idx, first);
    if (len > first) {
        memcpy(dst + first, ring_buf, len - first);
    }
}

/* Append one record. Builds it in a small stack buffer first so a wrap can
 * never write past the end of the ring. */
static void ring_write_record(const char *tag, int level, const char *msg,
                              int msg_len) {
    if (!msg) return;
    if (!tag) tag = "";

    char rec[256];
    const char lvl = (level >= 4) ? 'E'
                   : (level >= 3) ? 'W'
                   : (level >= 2) ? 'I'
                   :               'D';
    const unsigned long ms = millis();
    int n = snprintf(rec, sizeof(rec), "[%lu.%03lu][%c][%s] ",
                     ms / 1000, ms % 1000, lvl, tag);
    if (n < 0) return;
    if (n > (int)sizeof(rec) - 2) n = (int)sizeof(rec) - 2;

    int room = (int)sizeof(rec) - 1 - n;
    if (msg_len > room) msg_len = room;
    if (msg_len < 0) msg_len = 0;
    memcpy(rec + n, msg, msg_len);
    n += msg_len;

    if (n == 0 || rec[n - 1] != '\n') {
        if (n < (int)sizeof(rec) - 1) rec[n++] = '\n';
    }

    /* Mirror to the console while it is usable. The ring keeps working when
     * this is disabled; it is the only blocking sink. */
    if (serial_enabled) {
        Serial.write((const uint8_t *)rec, n);
    }

    if (!ring_buf || !ring_mutex || !ring_size) {
        legacy_store(rec, n);
        return;
    }

    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;  /* overloaded: drop rather than block the caller */
    }

    for (int i = 0; i < n; i++) {
        ring_buf[ring_head] = (uint8_t)rec[i];
        ring_head++;
        if (ring_head == ring_size) ring_head = 0;
        ring_total++;
        if (ring_used < ring_size) ring_used++;
    }

    xSemaphoreGive(ring_mutex);
}

/* ── public ring API ─────────────────────────────────────────────────────── */

void tpl_debug_ring_write(const char *tag, int level, const char *fmt, ...) {
    if (!fmt) return;

    char msg[192];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len > (int)sizeof(msg) - 1) len = (int)sizeof(msg) - 1;

    ring_write_record(tag, level, msg, len);
}

const char *tpl_debug_ring_dump(int *out_len) {
    if (!ring_buf || !ring_mutex || !ring_size) {
        if (out_len) *out_len = 0;
        return "";
    }

    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        if (out_len) *out_len = 0;
        return "";
    }

    const uint32_t used = ring_used;
    static char *dump_buf = NULL;
    static size_t dump_alloc = 0;

    if (used + 1 > dump_alloc) {
        char *nb = (char *)heap_caps_malloc(used + 1,
                                            MALLOC_CAP_SPIRAM |
                                            MALLOC_CAP_8BIT);
        if (!nb) {
            /* No PSRAM (or it is exhausted): the ring may be in internal RAM. */
            nb = (char *)heap_caps_malloc(used + 1, MALLOC_CAP_8BIT);
        }
        if (!nb) {
            xSemaphoreGive(ring_mutex);
            if (out_len) *out_len = 0;
            return "";
        }
        if (dump_buf) heap_caps_free(dump_buf);
        dump_buf = nb;
        dump_alloc = used + 1;
    }

    if (used) {
        ring_copy((uint8_t *)dump_buf, ring_floor(), used);
    }
    dump_buf[used] = '\0';

    xSemaphoreGive(ring_mutex);

    if (out_len) *out_len = (int)used;
    return dump_buf;
}

void tpl_debug_ring_clear(void) {
    if (!ring_mutex) return;
    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    ring_head = 0;
    ring_used = 0;
    ring_total = 0;
    for (int i = 0; i < TPL_DEBUG_WS_CLIENTS; i++) {
        ws_cursors[i].active = false;
        ws_cursors[i].seq = 0;
    }

    xSemaphoreGive(ring_mutex);
}

int tpl_debug_ring_length(void) {
    if (!ring_mutex) return 0;
    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    int len = (int)ring_used;
    xSemaphoreGive(ring_mutex);
    return len;
}

/* ── serial gate ─────────────────────────────────────────────────────────── */

void tpl_log_set_serial(bool enabled) {
    serial_enabled = enabled;
}

void tpl_debug_ring_set_serial(bool enabled) {
    serial_enabled = enabled;
}

/* ── WebSocket drain ─────────────────────────────────────────────────────── */

int tpl_debug_ring_ws_drain(uint8_t client, char *out_buf, int buf_len,
                            uint32_t *dropped) {
    if (dropped) *dropped = 0;
    if (!ring_buf || !ring_mutex || !ring_size || !out_buf || buf_len < 2) return 0;
    if (client >= TPL_DEBUG_WS_CLIENTS) return 0;

    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return 0;

    const uint32_t head = ring_total;
    const uint32_t floor = ring_floor();
    WsCursor &c = ws_cursors[client];

    if (!c.active) {
        /* New client: backfill recent context, not the whole ring. */
        c.active = true;
        const uint32_t backlog = (ring_used < 8192) ? ring_used : 8192;
        c.seq = head - backlog;
    }

    uint32_t drop = 0;

    /* Cursor was overwritten while the client was away. */
    if ((int32_t)(c.seq - floor) < 0) {
        drop += floor - c.seq;
        c.seq = floor;
    }

    uint32_t avail = head - c.seq;

    /* Bound per tick. Anything beyond the cap is a real gap for the client. */
    if (avail > 8192) {
        drop += avail - 8192;
        c.seq += avail - 8192;
        avail = 8192;
    }

    /* Reserve room for a dropped marker so both fit in the caller's buffer. */
    const int reserve = drop ? 32 : 0;
    if (buf_len <= reserve + 1) {
        xSemaphoreGive(ring_mutex);
        return 0;
    }
    const int room = buf_len - 1 - reserve;
    if ((int)avail > room) {
        uint32_t skip = avail - (uint32_t)room;
        drop += skip;
        c.seq += skip;
        avail -= skip;
    }

    int n = 0;
    if (drop) {
        n = snprintf(out_buf, buf_len, "{\"dropped\":%lu}\n",
                     (unsigned long)drop);
        if (n < 0) n = 0;
        if (n > buf_len - 1) n = buf_len - 1;
    }
    if (avail) {
        ring_copy((uint8_t *)out_buf + n, c.seq, avail);
        c.seq += avail;
        n += (int)avail;
    }
    out_buf[n] = '\0';

    xSemaphoreGive(ring_mutex);

    if (dropped) *dropped = drop;
    return n;
}

void tpl_debug_ring_ws_disconnect(uint8_t client) {
    if (client >= TPL_DEBUG_WS_CLIENTS) return;
    if (!ring_mutex) return;
    if (xSemaphoreTake(ring_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    ws_cursors[client].active = false;
    ws_cursors[client].seq = 0;
    xSemaphoreGive(ring_mutex);
}

/* ── capture hook ────────────────────────────────────────────────────────── */

/* esp_log_set_vprintf() covers the ESP-IDF log calls (WiFi, lwIP, drivers,
 * esp_lcd, …). Arduino's Serial.print() writes the UART directly and does not
 * pass through a vprintf hook on IDF 4.4, so project code should use TPL_LOGx
 * for records that must reach the ring. */
static int debug_log_vprintf(const char *fmt, va_list args) {
    char tmp[256];
    int len = vsnprintf(tmp, sizeof(tmp), fmt, args);
    if (len < 0) return len;

    int level = TPL_LOG_LEVEL_INFO;
    if (len >= 2 && tmp[1] == ' ') {
        switch (tmp[0]) {
            case 'E': level = TPL_LOG_LEVEL_ERROR; break;
            case 'W': level = TPL_LOG_LEVEL_WARN; break;
            case 'D': level = TPL_LOG_LEVEL_DEBUG; break;
            default:  level = TPL_LOG_LEVEL_INFO; break;
        }
    }
    int mlen = (len < (int)sizeof(tmp)) ? len : (int)sizeof(tmp) - 1;
    ring_write_record("idf", level, tmp, mlen);

    return len;
}

/* ── setup / endpoints ───────────────────────────────────────────────────── */

void tpl_debug_log_setup() {
    if (!log_mutex) log_mutex = xSemaphoreCreateMutex();
    if (!ring_mutex) ring_mutex = xSemaphoreCreateMutex();

    if (!ring_buf && ring_mutex) {
        size_t want = DEBUG_RING_SIZE;

        /* Preferred: the configured byte ring in PSRAM. */
        ring_buf = (uint8_t *)heap_caps_malloc(want,
                                               MALLOC_CAP_SPIRAM |
                                               MALLOC_CAP_8BIT);
        if (ring_buf) {
            ring_size = (uint32_t)want;
        } else if (DEBUG_LOG_FALLBACK_BYTES > 0) {
            /* No PSRAM, or it is needed elsewhere: small internal-RAM ring. */
            want = DEBUG_LOG_FALLBACK_BYTES;
            ring_buf = (uint8_t *)heap_caps_malloc(want,
                                                   MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT);
        if (ring_buf && ring_size) {
                ring_size = (uint32_t)want;
            }
        }

        if (ring_buf) {
            memset(ring_buf, 0, ring_size);
            ring_head = 0;
            ring_used = 0;
            ring_total = 0;
        } else {
            ring_size = 0;
            TPL_LOGW("log", "no byte ring available; using the %d-line buffer",
                     DEBUG_LOG_LINES);
        }
    }

    esp_log_set_vprintf(debug_log_vprintf);
}

void tpl_debug_log_register_endpoints() {
    tpl_server.on("/log", HTTP_GET, []() {
        tpl_server.sendHeader("Access-Control-Allow-Origin", "*");

        if (ring_buf) {
            int len = 0;
            const char *data = tpl_debug_ring_dump(&len);
            tpl_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
            tpl_server.send(200, "text/plain", "");
            if (len > 0) tpl_server.sendContent(data, (size_t)len);
            tpl_server.sendContent("");
            return;
        }

        /* legacy fallback */
        if (!log_mutex) {
            tpl_server.send(200, "text/plain", "debug log not initialized");
            return;
        }

        if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            tpl_server.send(503, "text/plain", "log busy");
            return;
        }
        int start, n;
        if (log_count < DEBUG_LOG_LINES) {
            start = 0;
            n = log_count;
        } else {
            start = log_head;
            n = DEBUG_LOG_LINES;
        }
        tpl_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        tpl_server.send(200, "text/plain", "");
        for (int i = 0; i < n; i++) {
            int idx = (start + i) % DEBUG_LOG_LINES;
            tpl_server.sendContent(log_buf + idx * DEBUG_LOG_LINE_SIZE);
            tpl_server.sendContent("\n");
        }
        xSemaphoreGive(log_mutex);
        tpl_server.sendContent("");
    });

    tpl_server.on("/log/clear", HTTP_GET, []() {
        tpl_debug_ring_clear();
        if (log_mutex &&
            xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            log_head = 0;
            log_count = 0;
            xSemaphoreGive(log_mutex);
        }
        tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
        tpl_server.send(200, "application/json", "{\"ok\":true}");
    });
}

#else

void tpl_debug_log_setup() {}

void tpl_debug_log_register_endpoints() {
    tpl_server.on("/log", HTTP_GET, []() {
        tpl_server.sendHeader("Access-Control-Allow-Origin", "*");
        tpl_server.send(200, "text/plain",
                        "debug log disabled (compile with -DUSE_TPL_DEBUG_LOG to enable)");
    });
}

void tpl_debug_ring_write(const char *tag, int level, const char *fmt, ...) {
    (void)tag;
    (void)level;
    if (!fmt) return;

    /* No ring in this build: preserve the previous console behaviour. */
    char msg[192];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (len <= 0) return;
    if (len > (int)sizeof(msg) - 1) len = (int)sizeof(msg) - 1;
    Serial.print(msg);
    if (msg[len - 1] != '\n') Serial.println();
}

/* TPL_LOGx fallback used when the ring is compiled out. */
void tpl_log_fallback(const char *fmt, ...) {
    if (!fmt) return;
    char msg[192];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (len <= 0) return;
    if (len > (int)sizeof(msg) - 1) len = (int)sizeof(msg) - 1;
    Serial.print(msg);
    if (msg[len - 1] != '\n') Serial.println();
}
const char *tpl_debug_ring_dump(int *out_len) {
    if (out_len) *out_len = 0;
    return "";
}
void tpl_debug_ring_clear(void) {}
void tpl_log_set_serial(bool) {}
void tpl_debug_ring_set_serial(bool) {}
int tpl_debug_ring_length(void) { return 0; }
int tpl_debug_ring_ws_drain(uint8_t, char *, int, uint32_t *dropped) {
    if (dropped) *dropped = 0;
    return 0;
}
void tpl_debug_ring_ws_disconnect(uint8_t) {}

#endif

#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>

/* ── Logging API ────────────────────────────────────────────────────────────
 * Always available: TPL_LOGx writes to the PSRAM ring when USE_TPL_DEBUG_LOG
 * is defined, and to Serial only otherwise. No RAM is reserved unless the
 * project opts in with -DUSE_TPL_DEBUG_LOG.
 */
#define TPL_LOG_LEVEL_DEBUG 0
#define TPL_LOG_LEVEL_INFO  2
#define TPL_LOG_LEVEL_WARN  3
#define TPL_LOG_LEVEL_ERROR 4

void tpl_debug_log_setup();
void tpl_debug_log_register_endpoints();

/* Write one record. Drops the record if the ring mutex cannot be taken within
 * 5 ms, so a logging call can never stall the caller. */
void tpl_debug_ring_write(const char *tag, int level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#ifdef USE_TPL_DEBUG_LOG
#define TPL_LOGD(tag, ...) tpl_debug_ring_write(tag, TPL_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define TPL_LOGI(tag, ...) tpl_debug_ring_write(tag, TPL_LOG_LEVEL_INFO, __VA_ARGS__)
#define TPL_LOGW(tag, ...) tpl_debug_ring_write(tag, TPL_LOG_LEVEL_WARN, __VA_ARGS__)
#define TPL_LOGE(tag, ...) tpl_debug_ring_write(tag, TPL_LOG_LEVEL_ERROR, __VA_ARGS__)
#else
/* Opt-in not set: keep every call site working by printing to Serial. No RAM
 * is reserved and no ring module is needed. */
void tpl_log_fallback(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
#define TPL_LOGD(tag, ...) ((void)(tag), tpl_log_fallback(__VA_ARGS__))
#define TPL_LOGI(tag, ...) ((void)(tag), tpl_log_fallback(__VA_ARGS__))
#define TPL_LOGW(tag, ...) ((void)(tag), tpl_log_fallback(__VA_ARGS__))
#define TPL_LOGE(tag, ...) ((void)(tag), tpl_log_fallback(__VA_ARGS__))
#endif
/* Full ring as a NUL-terminated linear buffer. Returns "" when the ring is
 * disabled. */
const char *tpl_debug_ring_dump(int *out_len);
void tpl_debug_ring_clear(void);
int  tpl_debug_ring_length(void);    /* bytes currently in ring */

/* Serial mirror. Disable it when the console can block (e.g. USB detached or
 * the USB/CAN mux has handed the port to CAN). The ring keeps working. */
void tpl_log_set_serial(bool enabled);
void tpl_debug_ring_set_serial(bool enabled);   /* legacy alias */

/* Per-client WebSocket drain. Call every 750 ms per connected client.
 * Returns the number of bytes placed in out_buf (NUL-terminated). When the
 * client has missed data, a {"dropped":N}\n line is prepended. A newly seen
 * client is backfilled from max(head - 8 KB, oldest). */
int  tpl_debug_ring_ws_drain(uint8_t client, char *out_buf, int buf_len,
                             uint32_t *dropped);
void tpl_debug_ring_ws_disconnect(uint8_t client);

#ifdef USE_TPL_DEBUG_LOG

/* ── PSRAM ring configuration (opt-in) ──────────────────────────────────────
 * One byte ring, newline-delimited records:
 *   [<uptime_s>.<ms>][<level>][<tag>] <message>\n
 * Records are variable length and overwrite the oldest bytes when full, so the
 * newest records are always readable.
 */
#ifndef DEBUG_LOG_BYTES
#define DEBUG_LOG_BYTES (1024 * 1024)   /* 1 MB, PSRAM */
#endif
#ifndef DEBUG_RING_SIZE
#define DEBUG_RING_SIZE DEBUG_LOG_BYTES
#endif

/* If PSRAM is unavailable (or the allocation fails), fall back to a small ring
 * in internal RAM. Set to 0 to disable the fallback entirely. */
#ifndef DEBUG_LOG_FALLBACK_BYTES
#define DEBUG_LOG_FALLBACK_BYTES (16 * 1024)
#endif

#ifndef DEBUG_LOG_SERIAL
#define DEBUG_LOG_SERIAL 1
#endif

/* ── legacy line buffer (kept for compatibility) ── */
#ifndef DEBUG_LOG_LINES
#define DEBUG_LOG_LINES 100
#endif
#ifndef DEBUG_LOG_LINE_SIZE
#define DEBUG_LOG_LINE_SIZE 160
#endif

#endif


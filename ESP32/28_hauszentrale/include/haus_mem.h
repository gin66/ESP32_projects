/**
 * @file haus_mem.h
 * @brief Allocation helpers that prefer PSRAM for CPU-only buffers.
 *
 * Internal SRAM is the scarce resource on this board (docs/memory_report.md).
 * Buffers that are never touched by DMA belong in PSRAM. The host harness has
 * no PSRAM, so it falls back to the C heap.
 */

#ifndef HAUS_MEM_H
#define HAUS_MEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAUS_HOST

#include <stdlib.h>

static inline void* haus_mem_alloc(size_t bytes) { return malloc(bytes); }
static inline void* haus_mem_realloc(void* p, size_t bytes) {
  return realloc(p, bytes);
}
static inline void haus_mem_free(void* p) { free(p); }

#else

#include <esp_heap_caps.h>

static inline void* haus_mem_alloc(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

static inline void* haus_mem_realloc(void* p, size_t bytes) {
  if (!p) return haus_mem_alloc(bytes);
  void* q = heap_caps_realloc(p, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!q)
    q = heap_caps_realloc(p, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return q;
}

static inline void haus_mem_free(void* p) { heap_caps_free(p); }

#endif

#ifdef __cplusplus
}
#endif

#endif /* HAUS_MEM_H */

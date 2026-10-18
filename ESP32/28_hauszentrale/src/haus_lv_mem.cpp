/**
 * @file haus_lv_mem.cpp
 * @brief LVGL 9 custom allocator: PSRAM first, internal SRAM as fallback.
 *
 * With LV_STDLIB_CLIB every allocation below
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4 KB) stays in internal SRAM. LVGL
 * widgets, styles and small buffers are exactly that size class, so the UI
 * consumed ~94 KB of internal RAM while PSRAM moved by a megabyte
 * (docs/memory_report.md). None of these objects are DMA sources, so PSRAM is
 * the right home.
 */

#include <lvgl.h>

#include "haus_mem.h"

extern "C" {

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  (void)mem;
  (void)bytes;
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void* lv_malloc_core(size_t size) { return haus_mem_alloc(size); }

void* lv_realloc_core(void* p, size_t new_size) {
  return haus_mem_realloc(p, new_size);
}

void lv_free_core(void* p) { haus_mem_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) {
#ifdef HAUS_HOST
  lv_memzero(mon_p, sizeof(*mon_p));
#else
  const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  lv_memzero(mon_p, sizeof(*mon_p));
  mon_p->total_size = total;
  mon_p->free_size = free_size;
  mon_p->free_biggest_size =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  mon_p->used_cnt = 0;
  mon_p->free_cnt = 0;
  mon_p->max_used = total - free_size;
  mon_p->used_pct = total ? (uint8_t)(((total - free_size) * 100u) / total) : 0;
#endif
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

}  // extern "C"

#include "lv_mem_psram.h"

#include "esp_heap_caps.h"

/* Route LVGL allocations to PSRAM to relieve pressure on internal DRAM.
 * Falls back to internal heap if PSRAM is exhausted, so LVGL never sees
 * a NULL where it would otherwise have had one.
 *
 * LVGL object access patterns (create/destroy + event dispatch) tolerate
 * PSRAM latency fine; only the framebuffer and draw buffers need to stay
 * in fast memory, and those are owned by display.c, not by LVGL's heap.
 *
 * This allocator exists to prevent StoreProhibited crashes on the dense
 * indoor page when LVGL's default internal pool runs out. */

#define PSRAM_CAPS   (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define FALLBACK_CAPS (MALLOC_CAP_8BIT)

void *lv_mem_psram_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, PSRAM_CAPS);
    if (p == NULL) {
        p = heap_caps_malloc(size, FALLBACK_CAPS);
    }
    return p;
}

void lv_mem_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

void *lv_mem_psram_realloc(void *ptr, size_t new_size)
{
    void *p = heap_caps_realloc(ptr, new_size, PSRAM_CAPS);
    if (p == NULL && new_size > 0) {
        p = heap_caps_realloc(ptr, new_size, FALLBACK_CAPS);
    }
    return p;
}

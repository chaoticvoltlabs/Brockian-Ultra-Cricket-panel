#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *lv_mem_psram_alloc(size_t size);
void  lv_mem_psram_free(void *ptr);
void *lv_mem_psram_realloc(void *ptr, size_t new_size);

#ifdef __cplusplus
}
#endif

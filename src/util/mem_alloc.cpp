#include "mem_alloc.h"

#include <esp_heap_caps.h>

// Uu tien PSRAM, thieu thi lui ve RAM trong. Board co PSRAM luon di duong
// thu nhat; duong thu hai chi de nhanh fallback SVGA/DRAM cua camera con song.
void *bigAlloc(size_t bytes) {
  void *p = NULL;
  if (psramFound()) p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!p) p = malloc(bytes);
  return p;
}

#ifndef MEM_ALLOC_H
#define MEM_ALLOC_H

// ============================================================================
// Cap phat bo nho lon
// ============================================================================
// Moi khoi can vai tram KB (buffer thu am, bo dem phat, ban sao JPEG) deu di
// qua day, de chi co MOT cho quyet dinh PSRAM hay RAM trong.
// ============================================================================
#include <Arduino.h>

// Uu tien PSRAM, thieu thi lui ve RAM trong. Giai phong bang free() nhu thuong.
void *bigAlloc(size_t bytes);

#endif  // MEM_ALLOC_H

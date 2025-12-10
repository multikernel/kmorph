#ifndef KMORPH_MEMBLOCK_H
#define KMORPH_MEMBLOCK_H

#include <stdint.h>

#define MEMORY_BLOCK_SIZE_PATH "/sys/devices/system/memory/block_size_bytes"
#define MEMORY_BLOCK_SIZE_FALLBACK (128ULL << 20)

/* The hot-plug granularity of memory, from sysfs (hex). */
int memblock_size(const char *path, uint64_t *block);

#endif

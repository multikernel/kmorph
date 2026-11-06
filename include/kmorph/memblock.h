#ifndef KMORPH_MEMBLOCK_H
#define KMORPH_MEMBLOCK_H

#include <stdint.h>

#include "kmorph/mkfs.h"
#include "kmorph/ranges.h"

#define MEMORY_BLOCK_SIZE_PATH "/sys/devices/system/memory/block_size_bytes"
#define MEMORY_BLOCK_SIZE_FALLBACK (128ULL << 20)

/* The size of a memory block, from sysfs (hex). */
int memblock_size(const char *path, uint64_t *block);

/* The unit memory moves into or out of this kernel in */
int memory_granule(const struct mkfs *fs, const char *block_size_path, uint64_t *granule);

/*
 * Memory a successor left out of its own when it took the machine over,
 * for the pool it arms its own successor from: no page of it can be
 * pinned, so the pool gets it whole however long the machine has run.
 */
#define POOL_CLAIM_PATH "/run/kmorph/pool-memory"

int pool_claim_write(const char *path, const struct range *r);
int pool_claim_read(const char *path, struct range *r);

#endif

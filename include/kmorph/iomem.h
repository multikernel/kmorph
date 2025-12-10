#ifndef KMORPH_IOMEM_H
#define KMORPH_IOMEM_H

#include "kmorph/ranges.h"

#define IOMEM_PATH "/proc/iomem"

int iomem_system_ram(const char *path, struct rangeset *ram);

#endif

#ifndef KMORPH_DUMP_H
#define KMORPH_DUMP_H

#include "kmorph/ranges.h"

#define DUMP_MEM_PATH "/dev/mem"

/*
 * Copy physical ranges into a sparse file whose offsets are physical
 * addresses, so the layout survives without a header.
 */
int dump_ranges(const char *mem_path, const struct rangeset *ranges, const char *out_path);

#endif

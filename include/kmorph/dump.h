#ifndef KMORPH_DUMP_H
#define KMORPH_DUMP_H

#include "kmorph/crashinfo.h"
#include "kmorph/ranges.h"

#define DUMP_MEM_PATH "/dev/mem"

struct dump_stats {
	size_t cpu_notes;	/* CPUs whose buffer held a saved note */
	bool vmcoreinfo;
};

/*
 * Write the ranges of mem_path as an ELF core in /proc/vmcore's format,
 * with the notes vi points at. A regular file gets holes for zero pages;
 * a FIFO or any other non-seekable output gets the same bytes in order.
 */
int dump_vmcore(const char *mem_path, const struct rangeset *ranges,
		const struct vmcore_info *vi, const char *out_path,
		struct dump_stats *stats);

#endif

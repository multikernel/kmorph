#ifndef KMORPH_ELFCORE_H
#define KMORPH_ELFCORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kmorph/ranges.h"

/*
 * The shape of a vmcore as /proc/vmcore presents one: an ELF64 core with
 * a PT_NOTE first and one PT_LOAD per memory range, physical addresses
 * in p_paddr, loads page aligned in the file. Pure layout: no I/O.
 */

#define ELFCORE_PAGE 4096ULL
#define ELFCORE_NO_VADDR ((uint64_t)-1)

struct elfcore_note {
	const void *data;	/* note records as the kernel wrote them */
	size_t len;
};

struct elfcore_layout {
	void *header;		/* ELF header, program headers, notes, zero padding */
	size_t header_len;	/* multiple of ELFCORE_PAGE */
	uint64_t *offsets;	/* file offset of each range's data, in rangeset order */
	uint64_t total;		/* file size */
};

/* Bytes of note records in buf, up to the terminating empty note; 0 if none. */
size_t elfcore_notes_len(const void *buf, size_t len);

int elfcore_layout(const struct rangeset *ranges, const struct elfcore_note *notes,
		   size_t nnotes, bool has_page_offset, uint64_t page_offset,
		   struct elfcore_layout *out);
void elfcore_layout_free(struct elfcore_layout *l);

#endif

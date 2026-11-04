#ifndef KMORPH_DMAR_H
#define KMORPH_DMAR_H

#include "kmorph/ranges.h"

#define DMAR_PATH "/sys/firmware/acpi/tables/DMAR"

/* The firmware's DMAR table, and the register set of each VT-d unit it names. */
struct dmar_info {
	struct rangelist units;
	void *table;
	size_t len;
};

#define DMAR_INFO_INIT { RANGELIST_INIT, NULL, 0 }

/* -ENOENT on a machine without the table. */
int dmar_read(const char *path, struct dmar_info *out);
void dmar_info_free(struct dmar_info *d);

#endif

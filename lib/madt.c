#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/file.h"
#include "kmorph/madt.h"

#define MADT_HEADER_LEN 44
#define MADT_ENTRY_LAPIC 0
#define MADT_ENTRY_X2APIC 9
#define MADT_CPU_ENABLED 0x1
#define MADT_CPU_ONLINE_CAPABLE 0x2

static uint32_t le32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int cpulist_push(struct cpulist *l, size_t *cap, uint64_t id)
{
	if (l->count == *cap) {
		size_t n = *cap ? *cap * 2 : 16;
		uint64_t *ids = realloc(l->ids, n * sizeof(*ids));

		if (!ids)
			return -ENOMEM;
		l->ids = ids;
		*cap = n;
	}
	l->ids[l->count++] = id;
	return 0;
}

static int madt_entry_cpu(const unsigned char *e, uint8_t len, uint64_t *id)
{
	uint32_t flags;

	switch (e[0]) {
	case MADT_ENTRY_LAPIC:
		if (len < 8)
			return 0;
		*id = e[3];
		flags = le32(e + 4);
		break;
	case MADT_ENTRY_X2APIC:
		if (len < 12)
			return 0;
		*id = le32(e + 4);
		flags = le32(e + 8);
		break;
	default:
		return 0;
	}
	return (flags & (MADT_CPU_ENABLED | MADT_CPU_ONLINE_CAPABLE)) ? 1 : 0;
}

int madt_read_cpus(const char *path, struct cpulist *out)
{
	unsigned char *table;
	size_t len, off, cap = 0;
	int ret;

	out->ids = NULL;
	out->count = 0;
	ret = file_read(path, (void **)&table, &len);
	if (ret)
		return ret;

	ret = -EINVAL;
	if (len < MADT_HEADER_LEN || memcmp(table, "APIC", 4) || le32(table + 4) > len)
		goto out;
	len = le32(table + 4);

	for (off = MADT_HEADER_LEN; off + 2 <= len; off += table[off + 1]) {
		uint8_t elen = table[off + 1];
		uint64_t id;

		if (elen < 2 || off + elen > len)
			goto out;
		if (madt_entry_cpu(table + off, elen, &id)) {
			ret = cpulist_push(out, &cap, id);
			if (ret)
				goto out;
		}
	}
	cpulist_normalize(out);
	ret = 0;
out:
	if (ret)
		cpulist_free(out);
	free(table);
	return ret;
}

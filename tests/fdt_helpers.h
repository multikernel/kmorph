#ifndef KMORPH_TEST_FDT_HELPERS_H
#define KMORPH_TEST_FDT_HELPERS_H

#include <stdlib.h>
#include <string.h>

#include "libfdt.h"

/* Build small trees for tests with the sequential-write API. */
static inline void *fdt_test_begin(size_t size)
{
	void *fdt = calloc(1, size);

	if (fdt_create(fdt, size) || fdt_finish_reservemap(fdt) || fdt_begin_node(fdt, ""))
		abort();
	return fdt;
}

static inline void fdt_test_finish(void *fdt)
{
	if (fdt_end_node(fdt) || fdt_finish(fdt))
		abort();
}

static inline void fdt_test_reg(void *fdt, uint64_t base, uint64_t size)
{
	fdt64_t reg[2] = { cpu_to_fdt64(base), cpu_to_fdt64(size) };

	if (fdt_property(fdt, "reg", reg, sizeof(reg)))
		abort();
}

static inline void fdt_test_u64_array(void *fdt, const char *name,
				      const uint64_t *v, size_t n)
{
	fdt64_t cells[64];
	size_t i;

	for (i = 0; i < n; i++)
		cells[i] = cpu_to_fdt64(v[i]);
	if (fdt_property(fdt, name, cells, n * sizeof(*cells)))
		abort();
}

static inline uint64_t fdt_test_get_u64(const void *fdt, int node, const char *name, int idx)
{
	int len;
	const fdt64_t *p = fdt_getprop(fdt, node, name, &len);

	if (!p || len < (idx + 1) * (int)sizeof(*p))
		return (uint64_t)-1;
	return fdt64_to_cpu(p[idx]);
}

#endif

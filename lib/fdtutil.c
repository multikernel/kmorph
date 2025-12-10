#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "libfdt.h"
#include "kmorph/fdtutil.h"

int fdtutil_check(const void *fdt, size_t len)
{
	if (len < sizeof(struct fdt_header) || fdt_check_header(fdt))
		return -EINVAL;
	return fdt_totalsize(fdt) <= len ? 0 : -EINVAL;
}

int fdtutil_read_reg(const void *fdt, int node, uint64_t *base, uint64_t *size)
{
	const fdt64_t *reg;
	int len;

	reg = fdt_getprop(fdt, node, "reg", &len);
	if (!reg || len != 2 * (int)sizeof(*reg))
		return -EINVAL;
	*base = fdt64_to_cpu(reg[0]);
	*size = fdt64_to_cpu(reg[1]);
	return 0;
}

int fdtutil_read_u64(const void *fdt, int node, const char *name, uint64_t *v)
{
	const fdt64_t *p;
	int len;

	p = fdt_getprop(fdt, node, name, &len);
	if (!p || len != (int)sizeof(*p))
		return -EINVAL;
	*v = fdt64_to_cpu(*p);
	return 0;
}

int fdtutil_cells_to_cpulist(const void *cells, size_t len, struct cpulist *out)
{
	const fdt64_t *p = cells;
	size_t i, n;

	if (len % sizeof(*p))
		return -EINVAL;
	n = len / sizeof(*p);
	out->ids = n ? malloc(n * sizeof(*out->ids)) : NULL;
	if (n && !out->ids)
		return -ENOMEM;
	for (i = 0; i < n; i++)
		out->ids[i] = fdt64_to_cpu(p[i]);
	out->count = n;
	return 0;
}

int fdtutil_read_cpulist(const void *fdt, int node, const char *name, struct cpulist *out)
{
	const void *p;
	int len;

	p = fdt_getprop(fdt, node, name, &len);
	if (!p)
		return -ENOENT;
	return fdtutil_cells_to_cpulist(p, len, out);
}

int fdtutil_prop_reg(void *fdt, uint64_t base, uint64_t size)
{
	fdt64_t reg[2] = { cpu_to_fdt64(base), cpu_to_fdt64(size) };

	return fdt_property(fdt, "reg", reg, sizeof(reg));
}

int fdtutil_prop_cpulist(void *fdt, const char *name, const struct cpulist *l)
{
	fdt64_t *cells;
	size_t i;
	int ret;

	cells = malloc(l->count * sizeof(*cells) + 1);
	if (!cells)
		return -FDT_ERR_INTERNAL;
	for (i = 0; i < l->count; i++)
		cells[i] = cpu_to_fdt64(l->ids[i]);
	ret = fdt_property(fdt, name, cells, l->count * sizeof(*cells));
	free(cells);
	return ret;
}

static int fdtutil_try_build(fdtutil_build_fn fn, void *arg, void *fdt, size_t size)
{
	int ret;

	ret = fdt_create(fdt, size);
	if (!ret)
		ret = fdt_finish_reservemap(fdt);
	if (!ret)
		ret = fn(fdt, arg);
	if (!ret)
		ret = fdt_finish(fdt);
	return ret;
}

int fdtutil_build(fdtutil_build_fn fn, void *arg, void **blob, size_t *len)
{
	size_t size = 16 * 1024;
	void *fdt = NULL;
	int ret;

	for (;;) {
		void *grown = realloc(fdt, size);

		if (!grown) {
			free(fdt);
			return -ENOMEM;
		}
		fdt = grown;

		ret = fdtutil_try_build(fn, arg, fdt, size);
		if (ret != -FDT_ERR_NOSPACE)
			break;
		if (size >= 4 * 1024 * 1024) {
			free(fdt);
			return -ENOSPC;
		}
		size *= 2;
	}
	if (ret) {
		free(fdt);
		return -EINVAL;
	}

	fdt_pack(fdt);
	*len = fdt_totalsize(fdt);
	*blob = fdt;
	return 0;
}

#ifndef KMORPH_FDTUTIL_H
#define KMORPH_FDTUTIL_H

#include <stddef.h>
#include <stdint.h>

#include "kmorph/parse.h"
#include "kmorph/ranges.h"

/* Header sanity plus fit within a buffer of the given length; 0 or -EINVAL. */
int fdtutil_check(const void *fdt, size_t len);

/* Reading */
int fdtutil_read_reg(const void *fdt, int node, uint64_t *base, uint64_t *size);
int fdtutil_read_u64(const void *fdt, int node, const char *name, uint64_t *v);
int fdtutil_cells_to_cpulist(const void *cells, size_t len, struct cpulist *out);
int fdtutil_read_cpulist(const void *fdt, int node, const char *name, struct cpulist *out);

/* Writing, on top of the sequential-write API */
int fdtutil_prop_reg(void *fdt, uint64_t base, uint64_t size);
int fdtutil_prop_cpulist(void *fdt, const char *name, const struct cpulist *l);

/*
 * Build a blob with a callback that issues fdt_begin_node("") .. fdt_end_node()
 * calls; the buffer grows until the callback no longer runs out of space.
 * The result is packed and owned by the caller.
 */
typedef int (*fdtutil_build_fn)(void *fdt, void *arg);
int fdtutil_build(fdtutil_build_fn fn, void *arg, void **blob, size_t *len);

int fdtutil_cells_to_rangelist(const void *cells, size_t len, struct rangelist *out);
int fdtutil_prop_regs(void *fdt, const struct rangelist *l);

#endif

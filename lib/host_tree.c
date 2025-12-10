#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libfdt.h"
#include "kmorph/dt.h"
#include "kmorph/fdtutil.h"
#include "kmorph/file.h"
#include "kmorph/host_tree.h"

#define TRY(expr) do { int _r = (expr); if (_r) return _r; } while (0)

int host_tree_emit(void *fdt, const struct host_tree *ht)
{
	size_t i;

	if (!ht->cpus.count || !ht->ram.count)
		return -EINVAL;
	TRY(fdtutil_prop_cpulist(fdt, "cpus", &ht->cpus));
	for (i = 0; i < ht->ram.count; i++) {
		char node[32];

		snprintf(node, sizeof(node), "memory@%llx",
			 (unsigned long long)ht->ram.r[i].base);
		TRY(fdt_begin_node(fdt, node));
		TRY(fdt_property_string(fdt, "device_type", "memory"));
		TRY(fdtutil_prop_reg(fdt, ht->ram.r[i].base, ht->ram.r[i].size));
		TRY(fdt_end_node(fdt));
	}
	if (ht->devices.count)
		TRY(dt_emit_pci_devices(fdt, &ht->devices));
	return 0;
}

static int read_reg_file(const char *dir, const char *node, struct rangeset *ram)
{
	char path[PATH_MAX];
	fdt64_t *cells;
	size_t len;
	int ret;

	snprintf(path, sizeof(path), "%s/%s/reg", dir, node);
	ret = file_read(path, (void **)&cells, &len);
	if (ret)
		return ret;
	ret = len == 2 * sizeof(*cells) ?
	      rangeset_add(ram, fdt64_to_cpu(cells[0]), fdt64_to_cpu(cells[1])) : -EINVAL;
	free(cells);
	return ret;
}

static int memory_node_filter(const struct dirent *de)
{
	return !strncmp(de->d_name, "memory@", 7);
}

int host_tree_read(const char *dir, struct host_tree *ht)
{
	char path[PATH_MAX];
	struct dirent **nodes;
	void *cells;
	size_t len;
	int n, i, ret;

	memset(ht, 0, sizeof(*ht));
	snprintf(path, sizeof(path), "%s/cpus", dir);
	ret = file_read(path, &cells, &len);
	if (!ret) {
		ret = fdtutil_cells_to_cpulist(cells, len, &ht->cpus);
		free(cells);
	}
	if (ret)
		goto fail;

	n = scandir(dir, &nodes, memory_node_filter, alphasort);
	if (n < 0) {
		ret = -errno;
		goto fail;
	}
	for (i = 0; i < n; i++) {
		if (!ret)
			ret = read_reg_file(dir, nodes[i]->d_name, &ht->ram);
		free(nodes[i]);
	}
	free(nodes);
	if (ret)
		goto fail;
	return 0;
fail:
	host_tree_free(ht);
	return ret;
}

void host_tree_free(struct host_tree *ht)
{
	cpulist_free(&ht->cpus);
	rangeset_free(&ht->ram);
	pci_list_free(&ht->devices);
}

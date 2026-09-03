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

static int emit_vmcore(void *fdt, const struct vmcore_info *vi)
{
	TRY(fdt_begin_node(fdt, "vmcore"));
	if (vi->has_page_offset)
		TRY(fdt_property_u64(fdt, "page-offset", vi->page_offset));
	if (vi->vmcoreinfo.size) {
		TRY(fdt_begin_node(fdt, "vmcoreinfo"));
		TRY(fdtutil_prop_reg(fdt, vi->vmcoreinfo.base, vi->vmcoreinfo.size));
		TRY(fdt_end_node(fdt));
	}
	if (vi->cpu_notes.count) {
		TRY(fdt_begin_node(fdt, "cpu-notes"));
		TRY(fdtutil_prop_regs(fdt, &vi->cpu_notes));
		TRY(fdt_end_node(fdt));
	}
	return fdt_end_node(fdt);
}

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
	if (vmcore_info_present(&ht->vmcore))
		TRY(emit_vmcore(fdt, &ht->vmcore));
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

static int read_optional(const char *dir, const char *rel, void **buf, size_t *len)
{
	char path[PATH_MAX];
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	ret = file_read(path, buf, len);
	if (ret == -ENOENT) {
		*buf = NULL;
		*len = 0;
		return 0;
	}
	return ret;
}

static int read_vmcore(const char *dir, struct vmcore_info *vi)
{
	void *buf;
	size_t len;
	int ret;

	ret = read_optional(dir, "vmcore/page-offset", &buf, &len);
	if (ret)
		return ret;
	if (buf) {
		if (len != sizeof(fdt64_t))
			ret = -EINVAL;
		else {
			vi->page_offset = fdt64_to_cpu(*(fdt64_t *)buf);
			vi->has_page_offset = true;
		}
		free(buf);
		if (ret)
			return ret;
	}

	ret = read_optional(dir, "vmcore/vmcoreinfo/reg", &buf, &len);
	if (ret)
		return ret;
	if (buf) {
		const fdt64_t *cells = buf;

		if (len != 2 * sizeof(*cells))
			ret = -EINVAL;
		else {
			vi->vmcoreinfo.base = fdt64_to_cpu(cells[0]);
			vi->vmcoreinfo.size = fdt64_to_cpu(cells[1]);
		}
		free(buf);
		if (ret)
			return ret;
	}

	ret = read_optional(dir, "vmcore/cpu-notes/reg", &buf, &len);
	if (ret)
		return ret;
	if (buf) {
		ret = fdtutil_cells_to_rangelist(buf, len, &vi->cpu_notes);
		free(buf);
	}
	return ret;
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

	ret = read_vmcore(dir, &ht->vmcore);
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
	vmcore_info_free(&ht->vmcore);
}

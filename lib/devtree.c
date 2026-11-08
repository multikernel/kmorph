#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libfdt.h"
#include "kmorph/devtree.h"
#include "kmorph/fdtutil.h"
#include "kmorph/file.h"

#define HOST_TREE_REL "chosen/multikernel,host-tree"
#define RESERVED_PROPERTY "chosen/multikernel,reserved-memory"
#define IOMMUS_REL "chosen/multikernel,iommus"

static void prop_path(char *buf, size_t len, const char *root, const char *rel)
{
	snprintf(buf, len, "%s/%s", root, rel);
}

bool devtree_has_host_tree(const char *root)
{
	char path[PATH_MAX];
	struct stat st;

	prop_path(path, sizeof(path), root, HOST_TREE_REL);
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void devtree_successor_name(const char *root, char *buf, size_t len)
{
	char path[PATH_MAX], *self;
	unsigned int gen = 0;
	int end = 0;

	prop_path(path, sizeof(path), root, "model");
	if (devtree_has_host_tree(root) && !file_read_string(path, &self)) {
		if (sscanf(self, "generation-%u%n", &gen, &end) != 1 || self[end])
			gen = 0;
		free(self);
	}
	snprintf(buf, len, "generation-%u", gen + 1);
}

static int read_cpulist_prop(const char *root, const char *rel, struct cpulist *out)
{
	char path[PATH_MAX];
	void *cells;
	size_t len;
	int ret;

	prop_path(path, sizeof(path), root, rel);
	ret = file_read(path, &cells, &len);
	if (ret)
		return ret;
	ret = fdtutil_cells_to_cpulist(cells, len, out);
	free(cells);
	return ret;
}

static int read_id_prop(const char *root, uint32_t *id)
{
	char path[PATH_MAX];
	void *cell;
	size_t len;
	int ret;

	prop_path(path, sizeof(path), root, "id");
	ret = file_read(path, &cell, &len);
	if (ret)
		return ret;
	if (len == sizeof(fdt32_t))
		*id = fdt32_to_cpu(*(fdt32_t *)cell);
	else
		ret = -EINVAL;
	free(cell);
	return ret;
}

/* (base, size) pairs the kernel publishes beside the host tree; absent means none. */
static int read_reserved(const char *root, struct rangeset *out)
{
	char path[PATH_MAX];
	fdt64_t *cells;
	size_t len, i;
	int ret;

	prop_path(path, sizeof(path), root, RESERVED_PROPERTY);
	ret = file_read(path, (void **)&cells, &len);
	if (ret == -ENOENT)
		return 0;
	if (ret)
		return ret;
	ret = len % (2 * sizeof(*cells)) ? -EINVAL : 0;
	for (i = 0; !ret && i + 1 < len / sizeof(*cells); i += 2)
		ret = rangeset_add(out, fdt64_to_cpu(cells[i]), fdt64_to_cpu(cells[i + 1]));
	free(cells);
	return ret;
}

int devtree_read_self(const char *root, struct self_info *s)
{
	char path[PATH_MAX];
	int ret;

	memset(s, 0, sizeof(*s));

	prop_path(path, sizeof(path), root, "model");
	ret = file_read_string(path, &s->name);
	if (!ret)
		ret = read_id_prop(root, &s->id);
	if (!ret)
		ret = read_cpulist_prop(root, "resources/cpus", &s->own_cpus);
	if (!ret)
		ret = read_reserved(root, &s->reserved);

	if (ret)
		self_info_free(s);
	return ret;
}

static int iommu_node_filter(const struct dirent *d)
{
	return !strncmp(d->d_name, "iommu@", 6);
}

int devtree_read_iommus(const char *root, struct rangelist *out)
{
	char dir[PATH_MAX], path[PATH_MAX + NAME_MAX + 8];
	struct dirent **nodes;
	fdt64_t *reg;
	size_t len;
	int n, i, ret = 0;

	prop_path(dir, sizeof(dir), root, IOMMUS_REL);
	n = scandir(dir, &nodes, iommu_node_filter, alphasort);
	if (n < 0)
		return errno == ENOENT ? 0 : -errno;
	for (i = 0; i < n; i++) {
		snprintf(path, sizeof(path), "%s/%s/reg", dir, nodes[i]->d_name);
		free(nodes[i]);
		if (ret || (ret = file_read(path, (void **)&reg, &len)))
			continue;
		ret = len == 2 * sizeof(*reg) ?
		      rangelist_add(out, fdt64_to_cpu(reg[0]), fdt64_to_cpu(reg[1])) : -EINVAL;
		free(reg);
	}
	free(nodes);
	if (ret)
		rangelist_free(out);
	return ret;
}

void self_info_free(struct self_info *s)
{
	free(s->name);
	cpulist_free(&s->own_cpus);
	rangeset_free(&s->reserved);
	memset(s, 0, sizeof(*s));
}

static int read_be_cells(const char *path, unsigned int cells, uint64_t *out)
{
	const fdt32_t *p;
	void *buf;
	size_t len, i;
	int ret;

	ret = file_read(path, &buf, &len);
	if (ret)
		return ret;
	p = buf;
	*out = 0;
	if (!cells || cells > 2 || len != cells * sizeof(*p))
		ret = -EINVAL;
	for (i = 0; !ret && i < cells; i++)
		*out = *out << 32 | fdt32_to_cpu(p[i]);
	free(buf);
	return ret;
}

static bool is_cpu_node(const char *cpus_dir, const char *name)
{
	char path[PATH_MAX], *type;
	size_t len;
	bool cpu;

	snprintf(path, sizeof(path), "%s/%s/device_type", cpus_dir, name);
	if (file_read(path, (void **)&type, &len))
		return false;
	cpu = len == sizeof("cpu") && !memcmp(type, "cpu", sizeof("cpu"));
	free(type);
	return cpu;
}

int devtree_read_cpus(const char *cpus_dir, struct cpulist *out)
{
	char path[PATH_MAX];
	struct dirent *de;
	uint64_t cells, id;
	size_t cap = 0;
	DIR *d;
	int ret;

	out->ids = NULL;
	out->count = 0;
	snprintf(path, sizeof(path), "%s/#address-cells", cpus_dir);
	ret = read_be_cells(path, 1, &cells);
	if (ret)
		return ret;
	d = opendir(cpus_dir);
	if (!d)
		return -errno;
	while (!ret && (de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		if (!is_cpu_node(cpus_dir, de->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s/reg", cpus_dir, de->d_name);
		ret = read_be_cells(path, cells, &id);
		if (!ret && out->count == cap) {
			uint64_t *ids = realloc(out->ids, (cap = cap ? cap * 2 : 16) * sizeof(*ids));

			if (ids)
				out->ids = ids;
			else
				ret = -ENOMEM;
		}
		if (!ret)
			out->ids[out->count++] = id;
	}
	closedir(d);
	if (!ret && !out->count)
		ret = -ENOENT;
	if (ret)
		cpulist_free(out);
	else
		cpulist_normalize(out);
	return ret;
}

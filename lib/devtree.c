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

void self_info_free(struct self_info *s)
{
	free(s->name);
	cpulist_free(&s->own_cpus);
	rangeset_free(&s->reserved);
	memset(s, 0, sizeof(*s));
}

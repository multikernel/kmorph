#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "kmorph/dt.h"
#include "kmorph/file.h"
#include "kmorph/memblock.h"

int memblock_size(const char *path, uint64_t *block)
{
	char *text, *end;
	int ret;

	ret = file_read_string(path, &text);
	if (ret)
		return ret;
	*block = strtoull(text, &end, 16);
	ret = (*end || !*block) ? -EINVAL : 0;
	free(text);
	return ret;
}

/*
 * A kernel that does not publish its granule takes memory in whole
 * blocks only, which is also a safe answer for any kernel that does.
 */
int memory_granule(const struct mkfs *fs, const char *block_size_path, uint64_t *granule)
{
	void *tree;
	size_t len;
	int ret;

	ret = mkfs_read_root_tree(fs, &tree, &len);
	if (!ret) {
		ret = len ? dt_read_memory_granule(tree, len, granule) : -ENOENT;
		free(tree);
	}
	if (!ret && *granule && !(*granule & (*granule - 1)))
		return 0;
	return memblock_size(block_size_path, granule);
}

int pool_claim_write(const char *path, const struct range *r)
{
	char text[64];
	int n = snprintf(text, sizeof(text), "%#" PRIx64 " %#" PRIx64 "\n", r->base, r->size);

	return file_write(path, text, n);
}

int pool_claim_read(const char *path, struct range *r)
{
	char *text;
	int ret;

	ret = file_read_string(path, &text);
	if (ret)
		return ret;
	if (sscanf(text, "%" SCNx64 " %" SCNx64, &r->base, &r->size) != 2 || !r->size)
		ret = -EINVAL;
	free(text);
	return ret;
}

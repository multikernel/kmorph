#include <errno.h>
#include <stdlib.h>

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

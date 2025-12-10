#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "kmorph/dump.h"

#define DUMP_CHUNK (1 << 20)

static int copy_range(int in, int out, char *buf, uint64_t base, uint64_t size)
{
	while (size) {
		size_t want = size < DUMP_CHUNK ? size : DUMP_CHUNK;
		ssize_t n = pread(in, buf, want, base);

		if (n < 0)
			return -errno;
		if (n == 0)
			return -EIO;
		if (pwrite(out, buf, n, base) != n)
			return -errno;
		base += n;
		size -= n;
	}
	return 0;
}

int dump_ranges(const char *mem_path, const struct rangeset *ranges, const char *out_path)
{
	char *buf;
	size_t i;
	int in, out, ret = 0;

	in = open(mem_path, O_RDONLY | O_CLOEXEC);
	if (in < 0)
		return -errno;
	out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (out < 0) {
		ret = -errno;
		close(in);
		return ret;
	}
	buf = malloc(DUMP_CHUNK);
	if (!buf)
		ret = -ENOMEM;

	for (i = 0; i < ranges->count && !ret; i++)
		ret = copy_range(in, out, buf, ranges->r[i].base, ranges->r[i].size);
	if (!ret && fsync(out) < 0)
		ret = -errno;

	free(buf);
	close(out);
	close(in);
	return ret;
}

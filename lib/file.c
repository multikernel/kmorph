#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/file.h"

int file_read(const char *path, void **buf, size_t *len)
{
	size_t cap = 4096, used = 0;
	char *data;
	int fd, ret = 0;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;

	data = malloc(cap + 1);
	if (!data) {
		close(fd);
		return -ENOMEM;
	}

	for (;;) {
		ssize_t n;

		if (used == cap) {
			char *grown = realloc(data, cap * 2 + 1);

			if (!grown) {
				ret = -ENOMEM;
				break;
			}
			data = grown;
			cap *= 2;
		}
		n = read(fd, data + used, cap - used);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			break;
		}
		if (n == 0)
			break;
		used += n;
	}
	close(fd);

	if (ret) {
		free(data);
		return ret;
	}
	data[used] = '\0';
	*buf = data;
	*len = used;
	return 0;
}

int file_read_string(const char *path, char **s)
{
	void *buf;
	size_t len;
	int ret;

	ret = file_read(path, &buf, &len);
	if (ret)
		return ret;
	*s = buf;
	while (len && (*s)[len - 1] == '\n')
		(*s)[--len] = '\0';
	return 0;
}

int file_read_u32(const char *path, uint32_t *v)
{
	char *s, *end;
	unsigned long n;
	int ret;

	ret = file_read_string(path, &s);
	if (ret)
		return ret;
	ret = -EINVAL;
	if (isdigit((unsigned char)*s)) {
		n = strtoul(s, &end, 10);
		if (!*end && n <= UINT32_MAX) {
			*v = n;
			ret = 0;
		}
	}
	free(s);
	return ret;
}

int file_write(const char *path, const void *buf, size_t len)
{
	const char *p = buf;
	int fd, ret = 0;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return -errno;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			break;
		}
		p += n;
		len -= n;
	}
	close(fd);
	return ret;
}

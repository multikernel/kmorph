#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "kmorph/cpio.h"

#define NEWC_HEADER 110

static int reserve(struct cpio *c, size_t more)
{
	size_t need = c->len + more;
	unsigned char *grown;

	if (need <= c->cap)
		return 0;
	while (c->cap < need)
		c->cap = c->cap ? 2 * c->cap : 4096;
	grown = realloc(c->data, c->cap);
	if (!grown)
		return -ENOMEM;
	c->data = grown;
	return 0;
}

static void pad4(struct cpio *c)
{
	while (c->len & 3)
		c->data[c->len++] = 0;
}

static int entry(struct cpio *c, const char *name, unsigned int mode,
		 const void *data, size_t len)
{
	size_t namesize = strlen(name) + 1;
	char hdr[NEWC_HEADER + 1];
	int ret;

	if (len > UINT32_MAX)
		return -EFBIG;
	ret = reserve(c, NEWC_HEADER + namesize + len + 8);
	if (ret)
		return ret;
	snprintf(hdr, sizeof(hdr),
		 "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
		 c->ino++, mode, 0u, 0u, 1u, 0u, (unsigned int)len, 0u, 0u, 0u, 0u,
		 (unsigned int)namesize, 0u);
	memcpy(c->data + c->len, hdr, NEWC_HEADER);
	c->len += NEWC_HEADER;
	memcpy(c->data + c->len, name, namesize);
	c->len += namesize;
	pad4(c);
	if (len)
		memcpy(c->data + c->len, data, len);
	c->len += len;
	pad4(c);
	return 0;
}

/* The kernel skips a file whose parent is missing, so name every ancestor. */
int cpio_add_file(struct cpio *c, const char *path, const void *data, size_t len,
		  unsigned int mode)
{
	char dir[256];
	const char *p;
	int ret;

	while (*path == '/')
		path++;
	if (!*path || strlen(path) >= sizeof(dir))
		return -EINVAL;
	for (p = strchr(path, '/'); p; p = strchr(p + 1, '/')) {
		memcpy(dir, path, p - path);
		dir[p - path] = '\0';
		ret = entry(c, dir, S_IFDIR | 0755, NULL, 0);
		if (ret)
			return ret;
	}
	return entry(c, path, S_IFREG | (mode & 07777), data, len);
}

int cpio_finish(struct cpio *c)
{
	return entry(c, "TRAILER!!!", 0, NULL, 0);
}

void cpio_free(struct cpio *c)
{
	free(c->data);
	c->data = NULL;
	c->len = c->cap = 0;
}

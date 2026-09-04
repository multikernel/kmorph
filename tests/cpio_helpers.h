#ifndef KMORPH_TEST_CPIO_HELPERS_H
#define KMORPH_TEST_CPIO_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cpio_entry {
	char name[256];
	unsigned int mode;
	const unsigned char *data;
	size_t len;
};

static unsigned long cpio_field(const unsigned char *hdr, int index)
{
	char buf[9];

	memcpy(buf, hdr + 6 + 8 * index, 8);
	buf[8] = '\0';
	return strtoul(buf, NULL, 16);
}

static size_t cpio_pad4(size_t n)
{
	return (n + 3) & ~(size_t)3;
}

/* 1: an entry was read and *off advanced; 0: the trailer; -1: malformed. */
static int cpio_next(const unsigned char *buf, size_t size, size_t *off, struct cpio_entry *e)
{
	const unsigned char *hdr = buf + *off;
	size_t namesize, filesize, name_off, data_off;

	if (*off + 110 > size || memcmp(hdr, "070701", 6))
		return -1;
	e->mode = cpio_field(hdr, 1);
	filesize = cpio_field(hdr, 6);
	namesize = cpio_field(hdr, 11);
	name_off = *off + 110;
	data_off = cpio_pad4(name_off + namesize);
	if (namesize == 0 || namesize >= sizeof(e->name) || data_off + filesize > size)
		return -1;
	memcpy(e->name, buf + name_off, namesize);
	e->data = buf + data_off;
	e->len = filesize;
	*off = cpio_pad4(data_off + filesize);
	return strcmp(e->name, "TRAILER!!!") ? 1 : 0;
}

#endif

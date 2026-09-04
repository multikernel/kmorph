#ifndef KMORPH_CPIO_H
#define KMORPH_CPIO_H

#include <stddef.h>

/*
 * A newc cpio archive in memory, the format the kernel unpacks as an
 * initramfs. Archives concatenate: the kernel unpacks each in turn into
 * the same root, so a small one appended to an image adds files to it.
 */
struct cpio {
	unsigned char *data;
	size_t len;
	size_t cap;
	unsigned int ino;
};

#define CPIO_INIT { NULL, 0, 0, 1 }

/* path may start with '/'; every ancestor directory is emitted before the file. */
int cpio_add_file(struct cpio *c, const char *path, const void *data, size_t len,
		  unsigned int mode);
int cpio_finish(struct cpio *c);
void cpio_free(struct cpio *c);

#endif

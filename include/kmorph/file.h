#ifndef KMORPH_FILE_H
#define KMORPH_FILE_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* All return 0 or a negative errno. */
int file_read(const char *path, void **buf, size_t *len);
int file_read_string(const char *path, char **s);
int file_read_u32(const char *path, uint32_t *v);
int file_write(const char *path, const void *buf, size_t len);

#endif

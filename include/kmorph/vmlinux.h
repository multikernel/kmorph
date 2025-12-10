#ifndef KMORPH_VMLINUX_H
#define KMORPH_VMLINUX_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The multikernel kexec loader takes an ELF vmlinux. A bzImage carries
 * one compressed inside; it is extracted with the matching system
 * decompressor into an anonymous file.
 */

bool vmlinux_is_bzimage(const void *image, size_t len);
int vmlinux_locate_payload(const void *image, size_t len, size_t *offset, size_t *payload_len);
const char *vmlinux_decompressor(const void *payload, size_t len);
size_t vmlinux_elf_size(const void *elf, size_t len);

/* Returns a readable fd positioned at 0, or -errno. */
int vmlinux_open(const char *path);

#endif

#ifndef KMORPH_MKFS_H
#define KMORPH_MKFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MKFS_ROOT "/sys/fs/multikernel"

/* The multikernel filesystem; root is injectable for tests. */
struct mkfs {
	const char *root;
};

/* Mount the filesystem at root unless its device_tree is already there. */
int mkfs_mount(const struct mkfs *fs);

/*
 * Write an overlay and report the transaction the kernel created for it.
 * 0 when the kernel applied it, -EIO when it failed, other negative errno
 * when the write itself failed. tx_id is set whenever a transaction exists.
 */
int mkfs_apply_overlay(const struct mkfs *fs, const void *dtbo, size_t len, int *tx_id);
int mkfs_write_baseline(const struct mkfs *fs, const void *dtb, size_t len);
int mkfs_read_root_tree(const struct mkfs *fs, void **dtb, size_t *len);
int mkfs_instance_id(const struct mkfs *fs, const char *name, uint32_t *id);
int mkfs_instance_status(const struct mkfs *fs, const char *name, char *buf, size_t len);

#endif

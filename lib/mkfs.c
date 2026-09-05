#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/mkfs.h"

static void mkfs_path(const struct mkfs *fs, char *buf, size_t len, const char *rel)
{
	snprintf(buf, len, "%s/%s", fs->root, rel);
}

static int mkfs_latest_tx(const struct mkfs *fs)
{
	char path[PATH_MAX];
	struct dirent *de;
	DIR *dir;
	int latest = -1;

	mkfs_path(fs, path, sizeof(path), "overlays");
	dir = opendir(path);
	if (!dir)
		return -errno;

	while ((de = readdir(dir))) {
		char *end;
		long id;

		if (strncmp(de->d_name, "tx_", 3) || !isdigit((unsigned char)de->d_name[3]))
			continue;
		id = strtol(de->d_name + 3, &end, 10);
		if (!*end && id > latest)
			latest = id;
	}
	closedir(dir);
	return latest;
}

int mkfs_mount(const struct mkfs *fs)
{
	char path[PATH_MAX];

	mkfs_path(fs, path, sizeof(path), "device_tree");
	if (access(path, F_OK) == 0)
		return 0;
	mkdir(fs->root, 0755);
	if (mount("none", fs->root, "multikernel", 0, NULL) < 0)
		return -errno;
	return 0;
}

int mkfs_apply_overlay(const struct mkfs *fs, const void *dtbo, size_t len, int *tx_id)
{
	char path[PATH_MAX], rel[64];
	char *status;
	int tx, ret;

	*tx_id = -1;
	mkfs_path(fs, path, sizeof(path), "overlays/new");
	ret = file_write(path, dtbo, len);
	if (ret)
		return ret;

	tx = mkfs_latest_tx(fs);
	if (tx < 0)
		return -EIO;
	*tx_id = tx;

	snprintf(rel, sizeof(rel), "overlays/tx_%d/status", tx);
	mkfs_path(fs, path, sizeof(path), rel);
	ret = file_read_string(path, &status);
	if (ret)
		return ret;
	ret = strcmp(status, "applied") ? -EIO : 0;
	free(status);
	return ret;
}

int mkfs_tx_reason(const struct mkfs *fs, int tx_id, char *buf, size_t len)
{
	char path[PATH_MAX], rel[64], *reason;
	int ret;

	buf[0] = '\0';
	snprintf(rel, sizeof(rel), "overlays/tx_%d/reason", tx_id);
	mkfs_path(fs, path, sizeof(path), rel);
	ret = file_read_string(path, &reason);
	if (ret)
		return ret;
	snprintf(buf, len, "%s", reason);
	free(reason);
	return 0;
}

int mkfs_tx_rollback(const struct mkfs *fs, int tx_id)
{
	char path[PATH_MAX], rel[64];

	snprintf(rel, sizeof(rel), "overlays/tx_%d", tx_id);
	mkfs_path(fs, path, sizeof(path), rel);
	return rmdir(path) < 0 ? -errno : 0;
}

int mkfs_write_baseline(const struct mkfs *fs, const void *dtb, size_t len)
{
	char path[PATH_MAX];

	mkfs_path(fs, path, sizeof(path), "device_tree");
	return file_write(path, dtb, len);
}

int mkfs_read_root_tree(const struct mkfs *fs, void **dtb, size_t *len)
{
	char path[PATH_MAX];

	mkfs_path(fs, path, sizeof(path), "device_tree");
	return file_read(path, dtb, len);
}

int mkfs_instance_id(const struct mkfs *fs, const char *name, uint32_t *id)
{
	char path[PATH_MAX], rel[256];

	snprintf(rel, sizeof(rel), "instances/%s/id", name);
	mkfs_path(fs, path, sizeof(path), rel);
	return file_read_u32(path, id);
}

int mkfs_instance_status(const struct mkfs *fs, const char *name, char *buf, size_t len)
{
	char path[PATH_MAX], rel[256];
	char *status;
	int ret;

	snprintf(rel, sizeof(rel), "instances/%s/status", name);
	mkfs_path(fs, path, sizeof(path), rel);
	ret = file_read_string(path, &status);
	if (ret)
		return ret;
	snprintf(buf, len, "%s", status);
	free(status);
	return 0;
}

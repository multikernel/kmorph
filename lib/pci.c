#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/pci.h"

#define PCI_COMMAND_OFFSET 0x04
#define PCI_COMMAND_MASTER 0x04

static int pci_id_valid(const char *id)
{
	unsigned int d, b, s, f;
	char tail;

	return strlen(id) == 12 && sscanf(id, "%4x:%2x:%2x.%1x%c", &d, &b, &s, &f, &tail) == 4;
}

static int read_hex_attr(const char *sysfs_root, const char *id, const char *attr, uint32_t *v)
{
	char path[PATH_MAX], *text, *end;
	int ret;

	snprintf(path, sizeof(path), "%s/%s/%s", sysfs_root, id, attr);
	ret = file_read_string(path, &text);
	if (ret)
		return ret;
	*v = strtoul(text, &end, 16);
	ret = *end ? -EINVAL : 0;
	free(text);
	return ret;
}

static int pci_dir_filter(const struct dirent *de)
{
	return pci_id_valid(de->d_name);
}

int pci_list_all(const char *sysfs_root, struct pci_list *out)
{
	struct dirent **names;
	int n, i, ret = 0;

	out->devs = NULL;
	out->count = 0;
	n = scandir(sysfs_root, &names, pci_dir_filter, alphasort);
	if (n < 0)
		return -errno;

	out->devs = calloc(n + 1, sizeof(*out->devs));
	if (!out->devs)
		ret = -ENOMEM;
	for (i = 0; i < n; i++) {
		if (!ret)
			ret = pci_read_ids(sysfs_root, names[i]->d_name, &out->devs[out->count]);
		if (!ret)
			out->count++;
		free(names[i]);
	}
	free(names);
	if (ret)
		pci_list_free(out);
	return ret;
}

void pci_list_free(struct pci_list *l)
{
	free(l->devs);
	l->devs = NULL;
	l->count = 0;
}

int pci_read_ids(const char *sysfs_root, const char *pci_id, struct pci_ids *out)
{
	int ret;

	if (!pci_id_valid(pci_id))
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	snprintf(out->id, sizeof(out->id), "%s", pci_id);
	ret = read_hex_attr(sysfs_root, pci_id, "vendor", &out->vendor);
	if (!ret)
		ret = read_hex_attr(sysfs_root, pci_id, "device", &out->device);
	if (!ret)
		ret = read_hex_attr(sysfs_root, pci_id, "class", &out->class);
	return ret;
}

bool pci_is_bridge(const struct pci_ids *d)
{
	return d->class >> 16 == 0x06;
}

/* A sysfs attribute is written whole, and never created when absent. */
static int write_attr(const char *path, const char *value)
{
	size_t len = strlen(value);
	ssize_t n;
	int fd;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = write(fd, value, len);
	if (n < 0)
		n = -errno;
	close(fd);
	return n < 0 ? (int)n : (size_t)n == len ? 0 : -EIO;
}

int pci_set_autoprobe(const char *bus_root, bool on)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/drivers_autoprobe", bus_root);
	return write_attr(path, on ? "1" : "0");
}

int pci_probe_driver(const char *bus_root, const char *pci_id)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/drivers_probe", bus_root);
	return write_attr(path, pci_id);
}

int pci_reset_function(const char *bus_root, const char *pci_id)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/devices/%s/reset", bus_root, pci_id);
	return write_attr(path, "1");
}

int pci_clear_bus_master(const char *bus_root, const char *pci_id)
{
	char path[PATH_MAX];
	uint8_t cmd[2];
	int fd, ret = 0;

	snprintf(path, sizeof(path), "%s/devices/%s/config", bus_root, pci_id);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	if (pread(fd, cmd, sizeof(cmd), PCI_COMMAND_OFFSET) != sizeof(cmd))
		ret = -EIO;
	if (!ret && cmd[0] & PCI_COMMAND_MASTER) {
		cmd[0] &= ~PCI_COMMAND_MASTER;
		if (pwrite(fd, cmd, sizeof(cmd), PCI_COMMAND_OFFSET) != sizeof(cmd))
			ret = -EIO;
	}
	close(fd);
	return ret;
}

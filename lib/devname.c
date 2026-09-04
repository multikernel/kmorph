#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/devname.h"

bool devname_is_pci_id(const char *s)
{
	static const char pattern[] = "hhhh:hh:hh.h";
	size_t i;

	if (strlen(s) != sizeof(pattern) - 1)
		return false;
	for (i = 0; pattern[i]; i++) {
		if (pattern[i] == 'h' ? !isxdigit((unsigned char)s[i]) : s[i] != pattern[i])
			return false;
	}
	return true;
}

static const char *const classes[] = { "net", "block", "graphics" };

int devname_resolve(const char *class_root, const char *usb_root, const char *name,
		    char *pci_id, size_t len)
{
	char found[PATH_MAX], path[PATH_MAX];
	const char *base;
	char *p;
	int hits = 0;
	size_t i;

	if (!*name || strchr(name, '/'))
		return -EINVAL;
	for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s/%s", class_root, classes[i], name);
		if (access(path, F_OK) == 0 && hits++ == 0)
			strcpy(found, path);
	}
	snprintf(path, sizeof(path), "%s/%s", usb_root, name);
	if (access(path, F_OK) == 0 && hits++ == 0)
		strcpy(found, path);
	if (!hits)
		return -ENOENT;
	if (hits > 1)
		return -EEXIST;
	if (!realpath(found, path))
		return -errno;

	/* The nearest ancestor named like a PCI function is the one to hand over. */
	while ((p = strrchr(path, '/'))) {
		*p = '\0';
		base = strrchr(path, '/');
		base = base ? base + 1 : path;
		if (devname_is_pci_id(base)) {
			snprintf(pci_id, len, "%s", base);
			return 0;
		}
	}
	return -ENODEV;
}

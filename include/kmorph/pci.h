#ifndef KMORPH_PCI_H
#define KMORPH_PCI_H

#include <stddef.h>
#include <stdint.h>

#define PCI_SYSFS_ROOT "/sys/bus/pci/devices"

/* A PCI function as the pool baseline names it. */
struct pci_ids {
	char id[16];		/* dddd:bb:ss.f */
	uint32_t vendor;
	uint32_t device;
};

struct pci_list {
	struct pci_ids *devs;
	size_t count;
};

int pci_read_ids(const char *sysfs_root, const char *pci_id, struct pci_ids *out);

/* Every PCI function under sysfs_root, in name order. */
int pci_list_all(const char *sysfs_root, struct pci_list *out);
void pci_list_free(struct pci_list *l);

#endif

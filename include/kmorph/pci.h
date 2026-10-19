#ifndef KMORPH_PCI_H
#define KMORPH_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_BUS_ROOT "/sys/bus/pci"
#define PCI_SYSFS_ROOT PCI_BUS_ROOT "/devices"

/* A PCI function as the pool baseline names it. */
struct pci_ids {
	char id[16];		/* dddd:bb:ss.f */
	uint32_t vendor;
	uint32_t device;
	uint32_t class;		/* base class, subclass, prog-if */
};

struct pci_list {
	struct pci_ids *devs;
	size_t count;
};

int pci_read_ids(const char *sysfs_root, const char *pci_id, struct pci_ids *out);
bool pci_is_bridge(const struct pci_ids *d);

/* Through the PCI bus in sysfs, rooted at bus_root (PCI_BUS_ROOT) */
int pci_set_autoprobe(const char *bus_root, bool on);
int pci_probe_driver(const char *bus_root, const char *pci_id);
/* -ENOENT when the kernel knows no way to reset the function */
int pci_reset_function(const char *bus_root, const char *pci_id);
int pci_clear_bus_master(const char *bus_root, const char *pci_id);

/* Every PCI function under sysfs_root, in name order. */
int pci_list_all(const char *sysfs_root, struct pci_list *out);
void pci_list_free(struct pci_list *l);

#endif

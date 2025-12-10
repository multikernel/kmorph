#ifndef KMORPH_HOST_TREE_H
#define KMORPH_HOST_TREE_H

#include "kmorph/parse.h"
#include "kmorph/pci.h"
#include "kmorph/ranges.h"

/*
 * The host tree: what the machine holds, as the predecessor's user space
 * sees it. It travels as the multikernel,host-tree node of the
 * instance-create overlay's chosen node, and the kernel places it under
 * the successor's boot /chosen. The successor kernel enumerates its
 * possible CPUs from cpus at boot; after a takeover the successor's user
 * space takes everything here it does not own.
 *
 *   multikernel,host-tree {
 *       cpus = <u64 ...>;
 *       memory@X { device_type = "memory"; reg = <base size>; } ...
 *       devices { pci_dddd_bb_ss_f { device-type = "pci"; pci-id;
 *                                   vendor-id; device-id; } ... }
 *   }
 */

#define HOST_TREE_NODE "multikernel,host-tree"
#define HOST_TREE_DIR DEVTREE_CHOSEN "/" HOST_TREE_NODE
#define DEVTREE_CHOSEN "/proc/device-tree/chosen"

struct host_tree {
	struct cpulist cpus;
	struct rangeset ram;
	struct pci_list devices;
};

/* Properties and subnodes of the node, into a node open on fdt. */
int host_tree_emit(void *fdt, const struct host_tree *ht);
/* The node as /proc/device-tree exposes it: a directory of files. */
int host_tree_read(const char *dir, struct host_tree *ht);
void host_tree_free(struct host_tree *ht);

#endif

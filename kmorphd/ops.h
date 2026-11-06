#ifndef KMORPHD_OPS_H
#define KMORPHD_OPS_H

#include <stdbool.h>

#include "kmorph/devtree.h"
#include "kmorph/mkfs.h"
#include "kmorph/takeable.h"
#include "takeover.h"

#include "kmorph/memblock.h"
#include "kmorph/netsnap.h"

/*
 * The takeover operations bound to the kernel interfaces. Every path the
 * daemon touches is injectable, so the sequence runs against a fake tree
 * in tests.
 */
struct ops_env {
	const char *devtree_root;
	const char *sysfs_root;
	const char *iomem_path;
	const char *mem_path;
	const char *block_size_path;	/* for a kernel that does not publish its memory granule */
	const char *dump_path;		/* NULL: reap the predecessor's memory at once */
	const char *pci_bus;		/* the PCI bus in sysfs */
	bool reset_devices;		/* reset each adopted device before its driver binds */
	int (*fence_fn)(int mk_id);
	const struct netsnap *network;	/* identity of the NICs to adopt; NULL: none */
	const struct netsnap_env *net_env;
	const char *net_restored_path;	/* lists the interfaces put back, for the network manager */
	uint64_t next_memory;		/* the memory of a successor armed from here; 0: none */
	const char *pool_claim_path;	/* where the memory kept for its pool is recorded */
};

struct ops {
	struct ops_env env;
	struct mkfs fs;
	struct self_info self;
	struct host_tree host;
	struct rangelist iommus;	/* the host's VT-d units, claimed once its DMA is stopped */
	struct takeable takeable;
	struct rangeset adoptable;	/* takeable memory the kernel can hot-add */
	struct rangeset preserved;	/* adoptable memory left unclaimed for now */
	struct range pool_claim;	/* takeable memory kept for the next pool */
	const char **foreign;		/* PCI ids of the predecessor's devices */
	size_t foreign_count;
	bool devices_tried;
	size_t devices_adopted;
};

int ops_init(struct ops *o, const struct ops_env *env);
void ops_free(struct ops *o);

int ops_fence(struct ops *o);
int ops_adopt_devices(struct ops *o);
int ops_adopt(struct ops *o);
int ops_preserve(struct ops *o);
int ops_reap(struct ops *o);

/* Adapters for the state machine; on_state is left for the daemon to set. */
extern const struct takeover_ops ops_takeover_base;

#endif

#ifndef KMORPHD_OPS_H
#define KMORPHD_OPS_H

#include "kmorph/devtree.h"
#include "kmorph/mkfs.h"
#include "kmorph/takeable.h"
#include "takeover.h"

#include "kmorph/memblock.h"

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
	const char *block_size_path;
	const char *dump_path;		/* NULL: reap the predecessor's memory at once */
	int (*fence_fn)(int mk_id);
};

struct ops {
	struct ops_env env;
	struct mkfs fs;
	struct self_info self;
	struct host_tree host;
	struct takeable takeable;
	struct rangeset adoptable;	/* takeable memory in whole memory blocks */
	struct rangeset preserved;	/* adoptable memory left unclaimed for now */
};

int ops_init(struct ops *o, const struct ops_env *env);
void ops_free(struct ops *o);

int ops_fence(struct ops *o);
int ops_adopt(struct ops *o);
int ops_preserve(struct ops *o);
int ops_reap(struct ops *o);

/* Adapters for the state machine; on_state is left for the daemon to set. */
extern const struct takeover_ops ops_takeover_base;

#endif

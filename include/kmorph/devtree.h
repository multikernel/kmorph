#ifndef KMORPH_DEVTREE_H
#define KMORPH_DEVTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kmorph/parse.h"
#include "kmorph/ranges.h"

#define DEVTREE_ROOT "/proc/device-tree"

/* What a successor kernel learns about itself from its own boot tree. */
struct self_info {
	char *name;
	uint32_t id;
	struct cpulist own_cpus;
	struct rangeset reserved;	/* the kernel's control regions, never adoptable */
};

bool devtree_has_host_tree(const char *root);
int devtree_read_self(const char *root, struct self_info *s);
void self_info_free(struct self_info *s);

#endif

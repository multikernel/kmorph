#ifndef KMORPH_DEVTREE_H
#define KMORPH_DEVTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kmorph/parse.h"
#include "kmorph/ranges.h"

#define DEVTREE_ROOT "/proc/device-tree"
#define DEVTREE_CPUS DEVTREE_ROOT "/cpus"

/* What a successor kernel learns about itself from its own boot tree. */
struct self_info {
	char *name;
	uint32_t id;
	struct cpulist own_cpus;
	struct rangeset reserved;	/* the kernel's control regions, never adoptable */
};

bool devtree_has_host_tree(const char *root);
int devtree_read_self(const char *root, struct self_info *s);
/* The register sets of the host's VT-d units this kernel boots behind; none is no error. */
int devtree_read_iommus(const char *root, struct rangelist *out);
void self_info_free(struct self_info *s);
/* Every CPU a firmware tree names under its /cpus node, by reg (MPIDR on arm64). */
int devtree_read_cpus(const char *cpus_dir, struct cpulist *out);

#endif

#ifndef KMORPH_TAKEABLE_H
#define KMORPH_TAKEABLE_H

#include "kmorph/host_tree.h"

/* What the successor may claim after fencing the predecessor. */
struct takeable {
	struct rangeset memory;
	struct cpulist cpus;
};

/*
 * Machine RAM minus the successor's own minus the kernel's reserved
 * regions; every machine CPU the successor does not own and @faulty,
 * which may be NULL, does not name.
 */
int takeable_compute(const struct host_tree *ht, const struct rangeset *own_ram,
		     const struct rangeset *reserved, const struct cpulist *own_cpus,
		     const struct cpulist *faulty, struct takeable *t);
void takeable_free(struct takeable *t);

#endif

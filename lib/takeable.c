#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/takeable.h"

static int cpulist_difference(const struct cpulist *all, const struct cpulist *minus,
			      struct cpulist *out)
{
	size_t i;

	out->ids = malloc((all->count + 1) * sizeof(*out->ids));
	if (!out->ids)
		return -ENOMEM;
	out->count = 0;
	for (i = 0; i < all->count; i++)
		if (!cpulist_has(minus, all->ids[i]) && !cpulist_has(out, all->ids[i]))
			out->ids[out->count++] = all->ids[i];
	return 0;
}

int takeable_compute(const struct host_tree *ht, const struct rangeset *own_ram,
		     const struct rangeset *reserved, const struct cpulist *own_cpus,
		     struct takeable *t)
{
	int ret;

	memset(t, 0, sizeof(*t));
	ret = rangeset_copy(&t->memory, &ht->ram);
	if (!ret)
		ret = rangeset_subtract_set(&t->memory, own_ram);
	if (!ret)
		ret = rangeset_subtract_set(&t->memory, reserved);
	if (!ret)
		ret = cpulist_difference(&ht->cpus, own_cpus, &t->cpus);
	if (!ret && (!t->memory.count || !t->cpus.count))
		ret = -ENOENT;
	if (ret)
		takeable_free(t);
	return ret;
}

void takeable_free(struct takeable *t)
{
	rangeset_free(&t->memory);
	cpulist_free(&t->cpus);
}

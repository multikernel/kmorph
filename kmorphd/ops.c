#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/dt.h"
#include "kmorph/dump.h"
#include "kmorph/file.h"
#include "kmorph/iomem.h"
#include "kmorph/log.h"
#include "kmorph/memblock.h"
#include "kmorph/mksys.h"
#include "ops.h"

int ops_init(struct ops *o, const struct ops_env *env)
{
	char path[PATH_MAX];
	int ret;

	memset(o, 0, sizeof(*o));
	o->env = *env;
	o->fs.root = env->sysfs_root;

	ret = devtree_read_self(env->devtree_root, &o->self);
	if (ret) {
		log_err("cannot read this kernel's boot tree under %s: %s",
			env->devtree_root, strerror(-ret));
		return ret;
	}
	snprintf(path, sizeof(path), "%s/chosen/%s", env->devtree_root, HOST_TREE_NODE);
	ret = host_tree_read(path, &o->host);
	if (ret) {
		log_err("no host tree at %s: %s; kmorphd runs inside a successor armed with "
			"'kmorph arm'", path, strerror(-ret));
		self_info_free(&o->self);
		return ret;
	}
	log_info("instance %s (id %u): %zu own CPUs; host tree lists %zu CPUs, %zu RAM ranges; %zu reserved regions",
		 o->self.name, o->self.id, o->self.own_cpus.count,
		 o->host.cpus.count, o->host.ram.count, o->self.reserved.count);
	return 0;
}

void ops_free(struct ops *o)
{
	rangeset_free(&o->preserved);
	rangeset_free(&o->adoptable);
	takeable_free(&o->takeable);
	host_tree_free(&o->host);
	self_info_free(&o->self);
}

int ops_fence(struct ops *o)
{
	int ret = o->env.fence_fn(MK_ID_PREDECESSOR);

	if (ret)
		log_err("fence of the predecessor failed: %s", strerror(-ret));
	else
		log_info("predecessor fenced");
	return ret;
}

/*
 * What the fenced predecessor leaves takeable, and the part of it the
 * kernel can hot-add: memory joins in whole memory blocks, so the
 * slivers around control regions and the successor's own RAM stay
 * unclaimed, still readable through /dev/mem.
 */
static int plan_adoption(struct ops *o)
{
	struct rangeset own_ram = RANGESET_INIT;
	uint64_t block;
	int ret;

	takeable_free(&o->takeable);
	rangeset_free(&o->adoptable);

	ret = iomem_system_ram(o->env.iomem_path, &own_ram);
	if (!ret)
		ret = takeable_compute(&o->host, &own_ram, &o->self.reserved,
				       &o->self.own_cpus, &o->takeable);
	rangeset_free(&own_ram);
	if (ret) {
		log_err("cannot compute the takeable resources: %s", strerror(-ret));
		return ret;
	}

	ret = memblock_size(o->env.block_size_path, &block);
	if (!ret)
		ret = rangeset_align(&o->takeable.memory, block, &o->adoptable);
	if (ret) {
		log_err("cannot align to the memory block size: %s", strerror(-ret));
		return ret;
	}
	log_info("takeable: %zu CPUs, %llu MB in %zu ranges, %llu MB adoptable in %zu blocks",
		 o->takeable.cpus.count,
		 (unsigned long long)rangeset_total(&o->takeable.memory) >> 20,
		 o->takeable.memory.count,
		 (unsigned long long)rangeset_total(&o->adoptable) >> 20,
		 o->adoptable.count);
	return 0;
}

static int apply_adopt(struct ops *o, const struct cpulist *cpus, const struct rangeset *memory)
{
	void *dtbo;
	size_t len;
	int tx, ret;

	ret = dt_build_adopt(o->self.name, cpus, memory, &dtbo, &len);
	if (ret)
		return ret;
	ret = mkfs_apply_overlay(&o->fs, dtbo, len, &tx);
	free(dtbo);
	if (ret)
		log_err("adopt overlay failed (tx %d): %s", tx, strerror(-ret));
	else
		log_info("adopted %zu CPUs and %llu MB (tx %d)", cpus->count,
			 (unsigned long long)rangeset_total(memory) >> 20, tx);
	return ret;
}

/* With a dump configured the memory stays unclaimed until it is copied. */
int ops_adopt(struct ops *o)
{
	struct rangeset none = RANGESET_INIT;
	const struct rangeset *now = &o->adoptable;
	int ret;

	ret = plan_adoption(o);
	if (ret)
		return ret;
	if (o->env.dump_path) {
		ret = rangeset_copy(&o->preserved, &o->adoptable);
		if (ret)
			return ret;
		now = &none;
	}
	return apply_adopt(o, &o->takeable.cpus, now);
}

int ops_preserve(struct ops *o)
{
	struct dump_stats st;
	int ret;

	if (!o->env.dump_path || !o->preserved.count)
		return 0;
	ret = dump_vmcore(o->env.mem_path, &o->takeable.memory, &o->host.vmcore,
			  o->env.dump_path, &st);
	if (ret)
		log_err("dump to %s failed: %s; memory left in the pool", o->env.dump_path,
			strerror(-ret));
	else
		log_info("predecessor memory dumped to %s: %llu MB in %zu ranges, %zu CPU notes, vmcoreinfo %s",
			 o->env.dump_path,
			 (unsigned long long)rangeset_total(&o->takeable.memory) >> 20,
			 o->takeable.memory.count, st.cpu_notes,
			 st.vmcoreinfo ? "present" : "absent");
	return ret;
}

int ops_reap(struct ops *o)
{
	struct cpulist none = { NULL, 0 };
	int ret;

	if (!o->preserved.count)
		return 0;
	ret = apply_adopt(o, &none, &o->preserved);
	if (!ret)
		rangeset_free(&o->preserved);
	return ret;
}

static int tk_fence(void *ctx) { return ops_fence(ctx); }
static int tk_adopt(void *ctx) { return ops_adopt(ctx); }
static int tk_preserve(void *ctx) { return ops_preserve(ctx); }
static int tk_reap(void *ctx) { return ops_reap(ctx); }

const struct takeover_ops ops_takeover_base = {
	.fence = tk_fence,
	.adopt = tk_adopt,
	.preserve = tk_preserve,
	.reap = tk_reap,
};

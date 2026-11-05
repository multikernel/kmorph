#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kmorph/dt.h"
#include "kmorph/dump.h"
#include "kmorph/file.h"
#include "kmorph/iomem.h"
#include "kmorph/log.h"
#include "kmorph/memblock.h"
#include "kmorph/mksys.h"
#include "kmorph/pci.h"
#include "ops.h"

/*
 * Every function of the machine this kernel was not booted with, but
 * bridges: an adopted function sits on a root bus of its own, so there is
 * nothing for a bridge to bridge.
 */
static int list_foreign(struct ops *o)
{
	char path[PATH_MAX];
	size_t i;

	o->foreign = calloc(o->host.devices.count + 1, sizeof(*o->foreign));
	if (!o->foreign)
		return -ENOMEM;
	for (i = 0; i < o->host.devices.count; i++) {
		const struct pci_ids *d = &o->host.devices.devs[i];

		snprintf(path, sizeof(path), "%s/devices/%s", o->env.pci_bus, d->id);
		if (!pci_is_bridge(d) && access(path, F_OK))
			o->foreign[o->foreign_count++] = d->id;
	}
	return 0;
}

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
	ret = list_foreign(o);
	if (!ret)
		ret = devtree_read_iommus(env->devtree_root, &o->iommus);
	if (ret) {
		ops_free(o);
		return ret;
	}
	log_info("instance %s (id %u): %zu own CPUs; host tree lists %zu CPUs, %zu RAM ranges, "
		 "%zu PCI functions; %zu reserved regions",
		 o->self.name, o->self.id, o->self.own_cpus.count, o->host.cpus.count,
		 o->host.ram.count, o->host.devices.count, o->self.reserved.count);
	return 0;
}

void ops_free(struct ops *o)
{
	free(o->foreign);
	rangelist_free(&o->iommus);
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
 * kernel can hot-add: memory joins only in whole units of the kernel's
 * memory granule, so the slivers around control regions and the successor's
 * own RAM stay unclaimed, still readable through /dev/mem.
 */
static int plan_adoption(struct ops *o)
{
	struct rangeset own_ram = RANGESET_INIT;
	uint64_t granule;
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

	ret = memory_granule(&o->fs, o->env.block_size_path, &granule);
	if (!ret)
		ret = rangeset_align(&o->takeable.memory, granule, &o->adoptable);
	if (ret) {
		log_err("cannot align to the memory granule: %s", strerror(-ret));
		return ret;
	}
	log_info("takeable: %zu CPUs, %llu MB in %zu ranges, %llu MB adoptable in %zu ranges",
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

static int adopt_device(struct ops *o, const char *id)
{
	void *dtbo;
	size_t len;
	int tx = -1, ret;

	ret = dt_build_adopt_device(o->self.name, id, &dtbo, &len);
	if (!ret) {
		ret = mkfs_apply_overlay(&o->fs, dtbo, len, &tx);
		free(dtbo);
	}
	if (ret)
		log_err("device %s not adopted (tx %d): %s", id, tx, strerror(-ret));
	return ret;
}

enum reset_outcome { DEVICE_RESET, DEVICE_QUIESCED, DEVICE_LIVE };

/* A function the kernel cannot reset at least stops its DMA. */
static enum reset_outcome reset_device(const char *bus, const char *id, bool reset)
{
	int ret = reset ? pci_reset_function(bus, id) : -ENOENT;

	if (!ret)
		return DEVICE_RESET;
	if (ret != -ENOENT)
		log_warn("device %s not reset: %s", id, strerror(-ret));
	ret = pci_clear_bus_master(bus, id);
	if (!ret)
		return DEVICE_QUIESCED;
	log_err("device %s neither reset nor quiesced: %s", id, strerror(-ret));
	return DEVICE_LIVE;
}

/* Side by side, as a reset takes the function 100 ms or more. */
static void reset_devices(const char *bus, const char **ids, size_t n, bool reset,
			  size_t counts[3])
{
	pid_t *pids = calloc(n + 1, sizeof(*pids));
	size_t i;

	for (i = 0; i < n; i++) {
		pid_t pid = pids ? fork() : -1;

		if (!pid)
			_exit(reset_device(bus, ids[i], reset));
		if (pid < 0)
			counts[reset_device(bus, ids[i], reset)]++;
		if (pids)
			pids[i] = pid;
	}
	for (i = 0; pids && i < n; i++) {
		int status;

		if (pids[i] < 0)
			continue;
		if (waitpid(pids[i], &status, 0) == pids[i] && WIFEXITED(status) &&
		    WEXITSTATUS(status) <= DEVICE_LIVE)
			counts[WEXITSTATUS(status)]++;
		else
			counts[DEVICE_LIVE]++;
	}
	free(pids);
}

/*
 * The dead host's IOMMU units would translate the successor's DMA through
 * the host's page tables. A unit is claimed only once no function it may
 * still guard runs DMA the host set up, as the successor's tables would
 * let such a function write anywhere.
 */
static int claim_iommu(struct ops *o, uint64_t base)
{
	void *dtbo;
	size_t len;
	int ret, tx;

	ret = dt_build_iommu_add(o->self.name, base, &dtbo, &len);
	if (ret)
		return ret;
	ret = mkfs_apply_overlay(&o->fs, dtbo, len, &tx);
	free(dtbo);
	if (ret)
		log_err("IOMMU %#llx not claimed (tx %d): %s", (unsigned long long)base,
			tx, strerror(-ret));
	return ret;
}

static void claim_iommus(struct ops *o, bool all_quiet)
{
	const struct rangelist *units = &o->iommus;
	size_t i, claimed = 0;

	if (!all_quiet) {
		log_err("devices left untaken or live; %zu IOMMUs stay the dead host's",
			units->count);
		return;
	}
	for (i = 0; i < units->count; i++)
		if (!claim_iommu(o, units->r[i].base))
			claimed++;
	log_info("claimed %zu of %zu IOMMUs", claimed, units->count);
}

/*
 * A device that cannot be taken leaves the others and the takeover to go
 * on. The bus probes no driver while the devices join, so each driver
 * meets its device reset or quiesced rather than as the crash left it,
 * and behind IOMMUs the successor drives. Success is one line for them
 * all, as /dev/kmsg drops a burst of more than ten.
 */
int ops_adopt_devices(struct ops *o)
{
	const char **adopted = calloc(o->foreign_count + 1, sizeof(*adopted));
	size_t iommus = o->iommus.count, counts[3] = { 0 }, i;
	bool hold;
	int first = 0, ret;

	if (!adopted)
		return -ENOMEM;
	hold = o->env.reset_devices || iommus;
	if (hold && pci_set_autoprobe(o->env.pci_bus, false)) {
		log_err("cannot hold off drivers on %s; adopting devices as they are",
			o->env.pci_bus);
		hold = false;
	}
	for (i = 0; i < o->foreign_count; i++) {
		ret = adopt_device(o, o->foreign[i]);
		if (ret && !first)
			first = ret;
		if (!ret)
			adopted[o->devices_adopted++] = o->foreign[i];
	}
	if (!hold) {
		log_info("adopted %zu devices", o->devices_adopted);
		if (iommus)
			claim_iommus(o, false);
		goto out;
	}

	reset_devices(o->env.pci_bus, adopted, o->devices_adopted, o->env.reset_devices, counts);
	if (iommus)
		claim_iommus(o, !first && !counts[DEVICE_LIVE]);
	ret = pci_set_autoprobe(o->env.pci_bus, true);
	if (ret)
		log_err("drivers left held off on %s: %s", o->env.pci_bus, strerror(-ret));
	for (i = 0; i < o->devices_adopted; i++) {
		ret = pci_probe_driver(o->env.pci_bus, adopted[i]);
		if (ret)
			log_warn("no driver probed for %s: %s", adopted[i], strerror(-ret));
	}
	log_info("adopted %zu devices: %zu reset, %zu quiesced", o->devices_adopted,
		 counts[DEVICE_RESET], counts[DEVICE_QUIESCED]);
out:
	free(adopted);
	return first;
}

/*
 * Devices come first, so that whatever DMA the predecessor left running
 * is stopped before its memory is hot-added. With a dump configured the
 * memory stays unclaimed until it is copied.
 */
int ops_adopt(struct ops *o)
{
	struct rangeset none = RANGESET_INIT;
	const struct rangeset *now = &o->adoptable;
	int ret;

	if (!o->devices_tried) {
		o->devices_tried = true;
		ops_adopt_devices(o);
	}
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

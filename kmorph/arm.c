#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/dt.h"
#include "kmorph/log.h"
#include "kmorph/mksys.h"
#include "kmorph/pci.h"
#include "kmorph/host_tree.h"
#include "kmorph/iomem.h"
#include "kmorph/madt.h"
#include "kmorph/memblock.h"
#include "kmorph/vmlinux.h"
#include "arm.h"

const struct arm_hooks arm_default_hooks = {
	.open_kernel = vmlinux_open,
	.kexec_load = mksys_kexec_load,
	.exec = mksys_exec,
	.halt = mksys_halt,
	.unload = mksys_kexec_unload,
	.pci_root = PCI_SYSFS_ROOT,
	.block_size_path = MEMORY_BLOCK_SIZE_PATH,
	.madt_path = MADT_PATH,
	.iomem_path = IOMEM_PATH,
};

static bool strlist_has(const struct strlist *l, const char *s)
{
	size_t i;

	for (i = 0; i < l->count; i++)
		if (!strcmp(l->items[i], s))
			return true;
	return false;
}

/* A serial console means the legacy 8250 device; the kernel allows a spawn only listed platform devices. */
static void console_platform_devices(const struct kmorph_config *cfg, struct strlist *out)
{
	static char serial8250[] = "serial8250";
	static char *names[] = { serial8250 };

	out->items = names;
	out->count = cfg->console ? 1 : 0;
}

static bool strlist_has(const struct strlist *l, const char *s);

static int check_pool_fits(const struct pool_view *pool, const struct kmorph_config *cfg)
{
	struct strlist platform;
	size_t i;

	console_platform_devices(cfg, &platform);
	for (i = 0; i < platform.count; i++) {
		if (!strlist_has(&pool->platform_devices, platform.items[i])) {
			log_err("the pool holds no platform device %s; platform devices enter "
				"the pool only with its baseline", platform.items[i]);
			return -ENOENT;
		}
	}

	for (i = 0; i < cfg->cpus.count; i++) {
		if (!cpulist_has(&pool->free_cpus, cfg->cpus.ids[i])) {
			log_err("CPU %llu is not free in the pool",
				(unsigned long long)cfg->cpus.ids[i]);
			return -ENOENT;
		}
	}
	if (cfg->memory > pool->free_bytes) {
		log_err("pool has %llu MB free, %llu MB requested",
			(unsigned long long)pool->free_bytes >> 20,
			(unsigned long long)cfg->memory >> 20);
		return -ENOSPC;
	}
	return 0;
}

/* The baseline names devices by vendor and device id, read from sysfs. */
static int lookup_devices(const struct kmorph_config *cfg, const char *pci_root,
			  struct pci_list *out)
{
	size_t i;

	out->count = 0;
	out->devs = calloc(cfg->devices.count + 1, sizeof(*out->devs));
	if (!out->devs)
		return -ENOMEM;
	for (i = 0; i < cfg->devices.count; i++) {
		int ret = pci_read_ids(pci_root, cfg->devices.items[i], &out->devs[i]);

		if (ret) {
			log_err("PCI device %s: %s", cfg->devices.items[i], strerror(-ret));
			free(out->devs);
			out->devs = NULL;
			return ret;
		}
		out->count++;
	}
	return 0;
}

/*
 * A new pool holds the successor's memory plus one memory block: the
 * kernel carves its pool park area out of pool memory, so a pool sized
 * exactly to the successor comes up a few pages short.
 */
static int init_pool(const struct kmorph_config *cfg, const struct mkfs *fs,
		     const struct arm_hooks *h)
{
	struct pci_list devices;
	struct strlist platform;
	uint64_t block, pool_bytes;
	void *dtb;
	size_t len;
	int ret;

	if (memblock_size(h->block_size_path, &block))
		block = MEMORY_BLOCK_SIZE_FALLBACK;
	pool_bytes = cfg->memory + block;

	ret = lookup_devices(cfg, h->pci_root, &devices);
	if (ret)
		return ret;
	console_platform_devices(cfg, &platform);
	ret = dt_build_pool_baseline(&cfg->cpus, pool_bytes, &devices, &platform, &dtb, &len);
	free(devices.devs);
	if (!ret) {
		ret = mkfs_write_baseline(fs, dtb, len);
		free(dtb);
	}
	if (ret)
		log_err("pool initialisation failed: %s", strerror(-ret));
	else
		log_info("pool initialised with the successor's resources");
	return ret;
}

int arm_pool_add_devices(const struct kmorph_config *cfg, const struct mkfs *fs,
			 const struct pool_view *pool)
{
	struct strlist missing = { NULL, 0 };
	char **items;
	void *dtbo;
	size_t i, len;
	int tx, ret;

	items = calloc(cfg->devices.count + 1, sizeof(*items));
	if (!items)
		return -ENOMEM;
	missing.items = items;
	for (i = 0; i < cfg->devices.count; i++)
		if (!strlist_has(&pool->devices, cfg->devices.items[i]))
			items[missing.count++] = cfg->devices.items[i];
	if (!missing.count) {
		free(items);
		return 0;
	}

	ret = dt_build_pool_device_add(&missing, &dtbo, &len);
	if (!ret) {
		ret = mkfs_apply_overlay(fs, dtbo, len, &tx);
		free(dtbo);
	}
	if (ret)
		log_err("adding %zu device(s) to the pool failed (tx %d): %s",
			missing.count, tx, strerror(-ret));
	else
		log_info("%zu device(s) added to the pool", missing.count);
	ret = ret ? ret : (int)missing.count;
	free(items);
	return ret;
}

/* Use the pool when there is one, create it from the config when there is none. */
static int ensure_pool(const struct kmorph_config *cfg, const struct mkfs *fs,
		       const struct arm_hooks *h)
{
	struct pool_view pool;
	void *tree;
	size_t len;
	int ret;

	ret = mkfs_read_root_tree(fs, &tree, &len);
	if (ret) {
		log_err("cannot read %s/device_tree: %s", fs->root, strerror(-ret));
		return ret;
	}
	ret = len ? dt_read_pool(tree, len, &pool) : -ENOENT;
	free(tree);
	if (ret == -ENOENT)
		return init_pool(cfg, fs, h);
	if (ret) {
		log_err("cannot parse the pool device tree: %s", strerror(-ret));
		return ret;
	}
	ret = check_pool_fits(&pool, cfg);
	if (!ret)
		ret = arm_pool_add_devices(cfg, fs, &pool);
	pool_view_free(&pool);
	return ret < 0 ? ret : 0;
}

/*
 * The host tree the successor boots with: every CPU on the machine (from
 * the config, else the firmware's MADT), the machine's RAM, and its PCI
 * devices. The successor can only fence and adopt what is named here.
 */
static int build_host_tree(const struct kmorph_config *cfg, const struct arm_hooks *h,
			   struct host_tree *ht)
{
	int ret = 0;

	memset(ht, 0, sizeof(*ht));
	if (cfg->machine_cpus.count)
		ht->cpus = cfg->machine_cpus;
	else
		ret = madt_read_cpus(h->madt_path, &ht->cpus);
	if (!ht->cpus.count) {
		log_err("no machine CPU list: %s unreadable (%s) and machine_cpus unset",
			h->madt_path, strerror(-ret));
		return -ENOENT;
	}

	ret = iomem_system_ram(h->iomem_path, &ht->ram);
	if (ret || !ht->ram.count) {
		log_err("no machine RAM map in %s", h->iomem_path);
		ret = ret ? ret : -ENOENT;
		goto fail;
	}
	if (pci_list_all(h->pci_root, &ht->devices))
		log_warn("no PCI inventory under %s; host tree lists no devices", h->pci_root);

	log_info("host tree: %zu CPUs, %llu MB in %zu ranges, %zu PCI devices",
		 ht->cpus.count, (unsigned long long)rangeset_total(&ht->ram) >> 20,
		 ht->ram.count, ht->devices.count);
	return 0;
fail:
	if (ht->cpus.ids != cfg->machine_cpus.ids)
		cpulist_free(&ht->cpus);
	rangeset_free(&ht->ram);
	return ret;
}

/* The CPU list may be the config's own; only free what was read here. */
static void release_host_tree(const struct kmorph_config *cfg, struct host_tree *ht)
{
	if (ht->cpus.ids == cfg->machine_cpus.ids)
		ht->cpus.ids = NULL;
	host_tree_free(ht);
}

static int create_instance(const struct kmorph_config *cfg, const struct mkfs *fs,
			   const struct arm_hooks *h)
{
	struct strlist platform;
	struct host_tree ht;
	void *dtbo;
	size_t len;
	int tx, ret;

	console_platform_devices(cfg, &platform);
	ret = build_host_tree(cfg, h, &ht);
	if (ret)
		return ret;
	ret = dt_build_instance_create(cfg->name, &cfg->cpus, cfg->memory, &cfg->devices,
				       &platform, &ht, &dtbo, &len);
	release_host_tree(cfg, &ht);
	if (ret)
		return ret;
	ret = mkfs_apply_overlay(fs, dtbo, len, &tx);
	free(dtbo);
	if (ret)
		log_err("instance-create overlay failed (tx %d): %s", tx, strerror(-ret));
	return ret;
}

static int load_and_exec(const struct kmorph_config *cfg, const struct arm_hooks *h, int id)
{
	int kernel_fd, initrd_fd = -1, ret;

	kernel_fd = h->open_kernel(cfg->kernel);
	if (kernel_fd < 0) {
		log_err("cannot open kernel %s: %s", cfg->kernel, strerror(-kernel_fd));
		return kernel_fd;
	}
	if (cfg->initrd) {
		initrd_fd = open(cfg->initrd, O_RDONLY | O_CLOEXEC);
		if (initrd_fd < 0) {
			ret = -errno;
			log_err("cannot open initrd %s: %s", cfg->initrd, strerror(-ret));
			close(kernel_fd);
			return ret;
		}
	}

	ret = h->kexec_load(kernel_fd, initrd_fd, cfg->cmdline, id);
	close(kernel_fd);
	if (initrd_fd >= 0)
		close(initrd_fd);
	if (ret) {
		log_err("kexec_file_load failed: %s", strerror(-ret));
		return ret;
	}

	ret = h->exec(id);
	if (ret)
		log_err("exec of instance %d failed: %s", id, strerror(-ret));
	return ret;
}

int arm_run(const struct kmorph_config *cfg, const struct mkfs *fs, const struct arm_hooks *h)
{
	uint32_t id;
	int ret;

	if (!cfg->kernel || !cfg->cpus.count || !cfg->memory) {
		log_err("config needs kernel, cpus and memory to arm a successor");
		return -EINVAL;
	}

	ret = ensure_pool(cfg, fs, h);
	if (ret)
		return ret;
	ret = create_instance(cfg, fs, h);
	if (ret)
		return ret;
	ret = mkfs_instance_id(fs, cfg->name, &id);
	if (ret) {
		log_err("instance %s has no id after creation: %s", cfg->name, strerror(-ret));
		return ret;
	}
	log_info("instance %s created with id %u", cfg->name, id);

	ret = load_and_exec(cfg, h, id);
	if (ret) {
		log_err("successor left in place; run 'kmorph disarm' to remove it");
		return ret;
	}
	log_info("successor %s armed", cfg->name);
	return 0;
}

int disarm_run(const struct kmorph_config *cfg, const struct mkfs *fs, const struct arm_hooks *h)
{
	char status[32];
	void *dtbo;
	size_t len;
	uint32_t id;
	int tx, ret;

	ret = mkfs_instance_id(fs, cfg->name, &id);
	if (ret) {
		log_err("no instance named %s", cfg->name);
		return ret;
	}
	if (mkfs_instance_status(fs, cfg->name, status, sizeof(status)))
		snprintf(status, sizeof(status), "unknown");
	if (!strcmp(status, "active")) {
		ret = h->halt(id);
		if (ret) {
			log_err("halt of instance %u failed: %s", id, strerror(-ret));
			return ret;
		}
	}
	/* A halted instance keeps its image; the kernel refuses to remove it loaded. */
	if (!strcmp(status, "active") || !strcmp(status, "loaded")) {
		ret = h->unload(id);
		if (ret) {
			log_err("unload of instance %u failed: %s", id, strerror(-ret));
			return ret;
		}
	}

	ret = dt_build_instance_remove(cfg->name, &dtbo, &len);
	if (ret)
		return ret;
	ret = mkfs_apply_overlay(fs, dtbo, len, &tx);
	free(dtbo);
	if (ret)
		log_err("instance-remove overlay failed (tx %d): %s", tx, strerror(-ret));
	else
		log_info("successor %s disarmed", cfg->name);
	return ret;
}

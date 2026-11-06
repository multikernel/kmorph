#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/cpio.h"
#include "kmorph/crashinfo.h"
#include "kmorph/devname.h"
#include "kmorph/devtree.h"
#include "kmorph/dt.h"
#include "kmorph/log.h"
#include "kmorph/mksys.h"
#include "kmorph/pci.h"
#include "kmorph/file.h"
#include "kmorph/host_tree.h"
#include "kmorph/iomem.h"
#include "kmorph/madt.h"
#include "kmorph/memblock.h"
#include "kmorph/netsnap.h"
#include "kmorph/vmlinux.h"
#include "arm.h"

static int net_snapshot(const struct strlist *skip, char **text)
{
	char buf[NETSNAP_TEXT_MAX];
	int ret = netsnap_text(skip, buf, sizeof(buf));

	if (ret < 0)
		return ret;
	*text = strdup(buf);
	return *text ? ret : -ENOMEM;
}

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
	.vmcoreinfo_path = VMCOREINFO_PATH,
	.cpu_root = CPU_SYSFS_ROOT,
	.kcore_path = KCORE_PATH,
	.default_initrd = KMORPH_INITRD_PATH,
	.class_root = DEVNAME_CLASS_ROOT,
	.usb_root = DEVNAME_USB_ROOT,
	.own_host_tree = HOST_TREE_DIR,
	.dt_cpus = DEVTREE_CPUS,
	.pool_claim_path = POOL_CLAIM_PATH,
	.net_snapshot = net_snapshot,
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

static int write_baseline(const struct kmorph_config *cfg, const struct mkfs *fs,
			  uint64_t pool_bytes, const struct range *claim,
			  const struct pci_list *devices, const struct strlist *platform)
{
	void *dtb;
	size_t len;
	int ret;

	ret = dt_build_pool_baseline(&cfg->cpus, pool_bytes, claim, devices, platform, &dtb, &len);
	if (!ret) {
		ret = mkfs_write_baseline(fs, dtb, len);
		free(dtb);
	}
	return ret;
}

/*
 * A new pool holds the successor's memory plus one memory granule: the
 * kernel carves its pool park area out of pool
 * memory, so a pool sized exactly to the successor comes up a few pages
 * short. On a kernel that took the machine over, it starts from the
 * memory kmorphd kept out of this kernel for it.
 */
static int init_pool(const struct kmorph_config *cfg, const struct mkfs *fs,
		     const struct arm_hooks *h)
{
	struct pci_list devices;
	struct strlist platform;
	struct range claim;
	uint64_t granule, pool_bytes;
	bool claimed = false;
	int ret;

	if (memory_granule(fs, h->block_size_path, &granule))
		granule = MEMORY_BLOCK_SIZE_FALLBACK;
	pool_bytes = cfg->memory + granule;

	ret = lookup_devices(cfg, h->pci_root, &devices);
	if (ret)
		return ret;
	console_platform_devices(cfg, &platform);
	if (h->pool_claim_path && !pool_claim_read(h->pool_claim_path, &claim)) {
		ret = write_baseline(cfg, fs, pool_bytes, &claim, &devices, &platform);
		if (ret)
			log_warn("cannot claim the %llu MB kept at %#llx for the pool (%s); taking the pool from this kernel",
				 (unsigned long long)claim.size >> 20,
				 (unsigned long long)claim.base, strerror(-ret));
		claimed = !ret;
		unlink(h->pool_claim_path);
	}
	if (!claimed)
		ret = write_baseline(cfg, fs, pool_bytes, NULL, &devices, &platform);
	free(devices.devs);
	if (ret)
		log_err("pool initialisation failed: %s", strerror(-ret));
	else if (claimed)
		log_info("pool initialised with the successor's resources and the %llu MB kept at the takeover",
			 (unsigned long long)claim.size >> 20);
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

/* Absence only costs the dump its notes or direct map; arming goes on. */
static void read_crash_layout(const struct arm_hooks *h, struct vmcore_info *vi)
{
	int ret;

	ret = crashinfo_read_vmcoreinfo(h->vmcoreinfo_path, &vi->vmcoreinfo);
	if (ret)
		log_warn("no vmcoreinfo at %s (%s); the dump will carry none",
			 h->vmcoreinfo_path, strerror(-ret));
	ret = crashinfo_read_cpu_notes(h->cpu_root, &vi->cpu_notes);
	if (ret)
		log_warn("no crash_notes under %s (%s); the dump will carry no registers",
			 h->cpu_root, strerror(-ret));
	ret = crashinfo_read_page_offset(h->kcore_path, &vi->page_offset);
	if (ret)
		log_warn("no direct map in %s (%s); the dump will carry no virtual addresses",
			 h->kcore_path, strerror(-ret));
	else
		vi->has_page_offset = true;
}

/*
 * A kernel that took over booted without ACPI, from a host tree; the
 * machine is still the one that tree describes, so its CPUs are passed on
 * as they came.
 */
static void inherit_cpus_from_own_host_tree(const struct arm_hooks *h, struct host_tree *ht)
{
	struct host_tree own;

	if (!h->own_host_tree || host_tree_read(h->own_host_tree, &own))
		return;
	log_info("no ACPI tables; machine CPUs from %s", h->own_host_tree);
	ht->cpus = own.cpus;
	own.cpus = (struct cpulist){ NULL, 0 };
	host_tree_free(&own);
}

/* A machine booted from a firmware device tree has no MADT; its /cpus names every CPU. */
static void cpus_from_firmware_tree(const struct arm_hooks *h, struct host_tree *ht)
{
	if (h->dt_cpus && !devtree_read_cpus(h->dt_cpus, &ht->cpus))
		log_info("no ACPI tables; machine CPUs from %s", h->dt_cpus);
}

/*
 * The host tree the successor boots with: every CPU on the machine (from
 * the config, else the firmware's MADT or device tree), the machine's RAM,
 * and its PCI devices. The successor can only fence and adopt what is
 * named here.
 */
static int build_host_tree(const struct kmorph_config *cfg, const struct arm_hooks *h,
			   struct host_tree *ht)
{
	bool no_madt = false;
	int ret = 0;

	memset(ht, 0, sizeof(*ht));
	if (cfg->machine_cpus.count) {
		ht->cpus = cfg->machine_cpus;
	} else {
		ret = madt_read_cpus(h->madt_path, &ht->cpus);
		no_madt = ret == -ENOENT;
	}
	if (no_madt)
		inherit_cpus_from_own_host_tree(h, ht);
	if (no_madt && !ht->cpus.count)
		cpus_from_firmware_tree(h, ht);
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
	/* The node tells the predecessor kernel to save registers when it stops. */
	if (cfg->dump)
		read_crash_layout(h, &ht->vmcore);

	log_info("host tree: %zu CPUs, %llu MB in %zu ranges, %zu PCI devices, %zu CPU notes",
		 ht->cpus.count, (unsigned long long)rangeset_total(&ht->ram) >> 20,
		 ht->ram.count, ht->devices.count, ht->vmcore.cpu_notes.count);
	return 0;
fail:
	if (ht->cpus.ids != cfg->machine_cpus.ids)
		cpulist_free(&ht->cpus);
	rangeset_free(&ht->ram);
	vmcore_info_free(&ht->vmcore);
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

/*
 * The successor dumps through /dev/mem, and IO_STRICT_DEVMEM refuses the
 * busy "RAM buffer" its e820 code registers above its own RAM, which is
 * predecessor memory. iomem=relaxed lifts that without touching the
 * protection of the kernel's own pages.
 */
#define DUMP_CMDLINE_PARAM "iomem=relaxed"

static bool cmdline_has_word(const char *cmdline, const char *word)
{
	size_t n = strlen(word);
	const char *p = cmdline;

	while ((p = strstr(p, word))) {
		bool starts = p == cmdline || p[-1] == ' ';
		bool ends = p[n] == '\0' || p[n] == ' ';

		if (starts && ends)
			return true;
		p += n;
	}
	return false;
}

/* The configured command line, plus what the dump needs; caller frees. */
static char *successor_cmdline(const struct kmorph_config *cfg)
{
	const char *base = cfg->cmdline ? cfg->cmdline : "";
	char *out;

	if (!cfg->dump || cmdline_has_word(base, DUMP_CMDLINE_PARAM))
		return cfg->cmdline ? strdup(cfg->cmdline) : NULL;
	if (asprintf(&out, "%s%s" DUMP_CMDLINE_PARAM, base, *base ? " " : "") < 0)
		return NULL;
	return out;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		p += n;
		len -= n;
	}
	return 0;
}

bool arm_memory_fits(uint64_t memory, size_t image_len)
{
	return memory / 3 >= image_len;
}

/*
 * The successor gives the NICs it adopts the addresses and routes they
 * have now; a NIC it owns from boot is not the predecessor's to describe.
 */
static int add_network(const struct kmorph_config *cfg, const struct arm_hooks *h,
		       struct cpio *extra)
{
	char *text = NULL;
	int ret;

	if (!h->net_snapshot)
		return 0;
	ret = h->net_snapshot(&cfg->devices, &text);
	if (ret < 0) {
		log_warn("no network identity for the successor: %s", strerror(-ret));
		return 0;
	}
	if (ret > 0)
		log_info("network identity of %d interfaces handed to the successor", ret);
	ret = cpio_add_file(extra, NETSNAP_PATH, text, strlen(text), 0644);
	free(text);
	return ret;
}

int arm_stale_source(const char *image, char *out, size_t len)
{
	char manifest[PATH_MAX], *list, *line, *save;
	struct stat img, st;
	int stale = 0;

	if (snprintf(manifest, sizeof(manifest), "%s.files", image) >= (int)sizeof(manifest) ||
	    stat(image, &img) < 0 || file_read_string(manifest, &list))
		return 0;
	for (line = strtok_r(list, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		if (!lstat(line, &st) && st.st_mtime > img.st_mtime) {
			snprintf(out, len, "%s", line);
			stale = 1;
			break;
		}
	}
	free(list);
	return stale;
}

/*
 * The image, then the one thing only arm knows: the config. The kernel
 * takes a cpio header only at a 4-byte aligned offset and skips zeros
 * to reach one, so an image of odd length is padded.
 */
static int successor_initrd(const struct kmorph_config *cfg, const struct arm_hooks *h)
{
	static const char zeros[3];
	const char *path = cfg->initrd ? cfg->initrd : h->default_initrd;
	struct cpio extra = CPIO_INIT;
	char stale[PATH_MAX];
	void *image;
	size_t len;
	int fd, ret;

	ret = file_read(path, &image, &len);
	if (ret) {
		log_err("cannot read successor image %s: %s%s", path, strerror(-ret),
			cfg->initrd ? "" : "; 'kmorph init' builds it");
		return ret;
	}
	if (arm_stale_source(path, stale, sizeof(stale)))
		log_warn("%s changed since %s was built; its standby services run the old "
			 "version until rebuilt with kmorph-mkimage", stale, path);
	if (!arm_memory_fits(cfg->memory, len))
		log_warn("memory = %llu MB may not hold the %zu MB image once unpacked",
			 (unsigned long long)cfg->memory >> 20, len >> 20);
	ret = cpio_add_file(&extra, KMORPH_CONFIG_PATH, cfg->text, strlen(cfg->text), 0644);
	if (!ret)
		ret = add_network(cfg, h, &extra);
	if (!ret)
		ret = cpio_finish(&extra);
	if (ret)
		goto out;

	fd = memfd_create("kmorph-initrd", MFD_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		goto out;
	}
	ret = write_all(fd, image, len);
	if (!ret)
		ret = write_all(fd, zeros, (4 - (len & 3)) & 3);
	if (!ret)
		ret = write_all(fd, extra.data, extra.len);
	if (ret) {
		log_err("cannot assemble the successor initrd: %s", strerror(-ret));
		close(fd);
		goto out;
	}
	lseek(fd, 0, SEEK_SET);
	ret = fd;
out:
	free(image);
	cpio_free(&extra);
	return ret;
}

static int load_and_exec(const struct kmorph_config *cfg, const struct arm_hooks *h, int id,
			 int initrd_fd)
{
	int kernel_fd, ret;
	bool expected;
	char *cmdline;

	kernel_fd = h->open_kernel(cfg->kernel);
	if (kernel_fd < 0) {
		log_err("cannot open kernel %s: %s", cfg->kernel, strerror(-kernel_fd));
		return kernel_fd;
	}

	expected = cfg->cmdline || cfg->dump;
	cmdline = successor_cmdline(cfg);
	if (!cmdline && expected) {
		close(kernel_fd);
		log_err("cannot build the successor's command line: %s", strerror(ENOMEM));
		return -ENOMEM;
	}

	ret = h->kexec_load(kernel_fd, initrd_fd, cmdline, id);
	close(kernel_fd);
	free(cmdline);
	if (ret) {
		log_err("kexec_file_load failed: %s", strerror(-ret));
		return ret;
	}

	ret = h->exec(id);
	if (ret)
		log_err("exec of instance %d failed: %s", id, strerror(-ret));
	return ret;
}

/* Names from sysfs become the PCI addresses the kernel wants; the config itself is untouched. */
static int resolve_devices(const struct kmorph_config *cfg, const struct arm_hooks *h,
			   struct strlist *out)
{
	size_t i;

	out->items = calloc(cfg->devices.count + 1, sizeof(*out->items));
	if (!out->items)
		return -ENOMEM;
	out->count = 0;
	for (i = 0; i < cfg->devices.count; i++) {
		const char *name = cfg->devices.items[i];
		char id[16];
		int ret = 0;

		if (!devname_is_pci_id(name)) {
			ret = devname_resolve(h->class_root, h->usb_root, name, id, sizeof(id));
			if (ret == -ENOENT)
				log_err("no device named %s under %s or %s", name, h->class_root,
					h->usb_root);
			else if (ret == -EEXIST)
				log_err("%s names more than one device; use its PCI address", name);
			else if (ret == -ENODEV)
				log_err("%s is not a PCI device; only PCI functions can be handed over",
					name);
			else if (ret)
				log_err("cannot resolve %s: %s", name, strerror(-ret));
			else
				log_info("%s is %s", name, id);
			name = id;
		}
		if (!ret) {
			out->items[out->count] = strdup(name);
			if (!out->items[out->count])
				ret = -ENOMEM;
		}
		if (ret) {
			strlist_free(out);
			return ret;
		}
		out->count++;
	}
	return 0;
}

int arm_run(const struct kmorph_config *cfg, const struct mkfs *fs, const struct arm_hooks *h)
{
	struct kmorph_config resolved;
	struct strlist devices;
	uint32_t id;
	int initrd_fd = -1, ret;

	if (!cfg->kernel || !cfg->cpus.count || !cfg->memory) {
		log_err("config needs kernel, cpus and memory to arm a successor");
		return -EINVAL;
	}
	ret = resolve_devices(cfg, h, &devices);
	if (ret)
		return ret;
	resolved = *cfg;
	resolved.devices = devices;
	cfg = &resolved;

	ret = ensure_pool(cfg, fs, h);
	if (ret)
		goto out;
	initrd_fd = successor_initrd(cfg, h);
	if (initrd_fd < 0) {
		ret = initrd_fd;
		goto out;
	}
	ret = create_instance(cfg, fs, h);
	if (ret)
		goto out;
	ret = mkfs_instance_id(fs, cfg->name, &id);
	if (ret) {
		log_err("instance %s has no id after creation: %s", cfg->name, strerror(-ret));
		goto out;
	}
	log_info("instance %s created with id %u", cfg->name, id);

	ret = load_and_exec(cfg, h, id, initrd_fd);
	if (ret) {
		log_err("successor left in place; run 'kmorph disarm' to remove it");
		goto out;
	}
	log_info("successor %s armed", cfg->name);
out:
	if (initrd_fd >= 0)
		close(initrd_fd);
	strlist_free(&devices);
	return ret;
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

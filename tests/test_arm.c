#include <elf.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/file.h"
#include "kmorph/fdtutil.h"
#include "cpio_helpers.h"
#include "elf_helpers.h"
#include "kmorph/cpio.h"
#include "arm.h"

static char sysfs[] = "/tmp/kmorph-test-arm-XXXXXX";
static char pci_root[600], block_size[600], madt_path[600], iomem_path[600];
static char vmcoreinfo_path[600], cpu_root[600], kcore_path[600];
static char image_path[600], login_path[600];

struct calls {
	char seq[32];
	int loaded_id, exec_id, halt_id, unloaded_id;
	int kernel_fd, initrd_fd;
	char cmdline[64];
	unsigned char initrd[65536];
	size_t initrd_len;
};

static struct calls calls;

static void record(char c)
{
	size_t n = strlen(calls.seq);

	if (n + 1 < sizeof(calls.seq))
		calls.seq[n] = c;
}

static int fake_open_kernel(const char *path)
{
	(void)path;
	record('K');
	return 42;
}

static int fake_kexec_load(int kernel_fd, int initrd_fd, const char *cmdline, int mk_id)
{
	record('L');
	calls.kernel_fd = kernel_fd;
	calls.initrd_fd = initrd_fd;
	calls.loaded_id = mk_id;
	calls.initrd_len = 0;
	if (initrd_fd >= 0) {
		ssize_t n;

		lseek(initrd_fd, 0, SEEK_SET);
		n = read(initrd_fd, calls.initrd, sizeof(calls.initrd));
		calls.initrd_len = n > 0 ? (size_t)n : 0;
	}
	snprintf(calls.cmdline, sizeof(calls.cmdline), "%s", cmdline ? cmdline : "");
	return 0;
}

static int fake_exec(int mk_id) { record('E'); calls.exec_id = mk_id; return 0; }
static int fake_halt(int mk_id) { record('H'); calls.halt_id = mk_id; return 0; }
static int fake_unload(int mk_id) { record('U'); calls.unloaded_id = mk_id; return 0; }

static const struct arm_hooks hooks = {
	.open_kernel = fake_open_kernel,
	.kexec_load = fake_kexec_load,
	.exec = fake_exec,
	.halt = fake_halt,
	.unload = fake_unload,
	.pci_root = pci_root,
	.block_size_path = block_size,
	.madt_path = madt_path,
	.iomem_path = iomem_path,
	.vmcoreinfo_path = vmcoreinfo_path,
	.cpu_root = cpu_root,
	.kcore_path = kcore_path,
	.default_initrd = image_path,
};

static void put(const char *rel, const void *data, size_t len)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", sysfs, rel);
	CHECK_EQ(file_write(path, data, len), 0);
}

static void mkdir_rel(const char *rel)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", sysfs, rel);
	mkdir(path, 0755);
}

static void put_root_tree(uint64_t free_bytes)
{
	void *fdt = fdt_test_begin(2048);
	uint64_t free_cpus[] = { 12, 13, 14, 15 };

	fdt_begin_node(fdt, "resources");
	fdt_test_u64_array(fdt, "cpus", free_cpus, 4);
	fdt_begin_node(fdt, "memory@100000000");
	fdt_test_reg(fdt, 0x100000000, free_bytes);
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);
	put("device_tree", fdt, fdt_totalsize(fdt));
	free(fdt);
}

static void *read_blob(const char *rel, size_t *len)
{
	char path[512];
	void *blob;

	snprintf(path, sizeof(path), "%s/%s", sysfs, rel);
	CHECK_EQ(file_read(path, &blob, len), 0);
	return blob;
}

static struct kmorph_config cfg;

static void put_pci_device(const char *id, const char *vendor, const char *device)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", pci_root, id);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/%s/vendor", pci_root, id);
	CHECK_EQ(file_write(path, vendor, strlen(vendor)), 0);
	snprintf(path, sizeof(path), "%s/%s/device", pci_root, id);
	CHECK_EQ(file_write(path, device, strlen(device)), 0);
}


static const char image_init[] = "#!/bin/sh\necho init\n";
static unsigned char image[512];
static size_t image_len;

/* A static executable standing in for the console's login program. */
static void put_login(void)
{
	unsigned char elf[256];
	size_t len = fake_elf64(elf, 0);

	CHECK_EQ(file_write(login_path, elf, len), 0);
}

static void put_image(void)
{
	struct cpio c = CPIO_INIT;

	CHECK_EQ(cpio_add_file(&c, "init", image_init, sizeof(image_init) - 1, 0755), 0);
	CHECK_EQ(cpio_finish(&c), 0);
	CHECK(c.len <= sizeof(image));
	memcpy(image, c.data, c.len);
	image_len = c.len;
	CHECK_EQ(file_write(image_path, image, image_len), 0);
	cpio_free(&c);
}

/* The archive arm appended after the image; returns 1 with the entry named, or 0. */
static int appended_entry(const char *name, struct cpio_entry *e)
{
	size_t off = image_len;
	int ret;

	CHECK(calls.initrd_len > image_len);
	CHECK(memcmp(calls.initrd, image, image_len) == 0);
	while ((ret = cpio_next(calls.initrd, calls.initrd_len, &off, e)) == 1)
		if (!strcmp(e->name, name))
			return 1;
	return 0;
}

static void put_madt(const uint8_t *apic_ids, size_t n)
{
	unsigned char t[128];
	size_t off = 44, i;
	uint32_t len, one = 1;

	memset(t, 0, sizeof(t));
	memcpy(t, "APIC", 4);
	for (i = 0; i < n; i++) {
		t[off] = 0;
		t[off + 1] = 8;
		t[off + 2] = t[off + 3] = apic_ids[i];
		memcpy(t + off + 4, &one, 4);
		off += 8;
	}
	len = off;
	memcpy(t + 4, &len, 4);
	CHECK_EQ(file_write(madt_path, t, off), 0);
}


static void setup(void)
{
	char err[64];

	config_parse("name = successor\ncpus = 12-15\nmemory = 4GB\n"
		     "kernel = /boot/vmlinuz\ncmdline = quiet\n", &cfg, err, sizeof(err));
	memset(&calls, 0, sizeof(calls));
	put_image();
	put_login();
	mkdir_rel("overlays");
	put("overlays/new", "", 0);
	mkdir_rel("overlays/tx_7");
	put("overlays/tx_7/status", "applied\n", 8);
	mkdir_rel("instances");
	mkdir_rel("instances/successor");
	put("instances/successor/id", "5\n", 2);
	put("instances/successor/status", "active\n", 7);
	put_root_tree(8ULL << 30);
	{
		const uint8_t apics[] = { 0, 1, 2, 3 };
		static const char iomem[] = "00100000-7ffdbfff : System RAM\n";

		put_madt(apics, 4);
		CHECK_EQ(file_write(iomem_path, iomem, sizeof(iomem) - 1), 0);
	}
}

static void arm_creates_loads_and_execs(void)
{
	struct mkfs fs = { sysfs };
	void *dtbo;
	size_t len;
	int frag, ov, op, res;

	setup();
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.seq, "KLE");
	CHECK_EQ(calls.kernel_fd, 42);
	{
		struct cpio_entry e;

		CHECK(calls.initrd_fd >= 0);
		CHECK_EQ(appended_entry("etc/kmorph/kmorph.conf", &e), 1);
		CHECK_EQ(e.len, strlen(cfg.text));
		CHECK(memcmp(e.data, cfg.text, e.len) == 0);
	}
	CHECK_EQ(calls.loaded_id, 5);
	CHECK_EQ(calls.exec_id, 5);
	CHECK_STREQ(calls.cmdline, "quiet");

	dtbo = read_blob("overlays/new", &len);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/instances");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "instance-create");
	CHECK_STREQ(fdt_getprop(dtbo, op, "instance-name", NULL), "successor");
	res = fdt_subnode_offset(dtbo, op, "resources");
	CHECK_EQ(fdt_test_get_u64(dtbo, res, "memory-bytes", 0), 4ULL << 30);
	free(dtbo);
	config_free(&cfg);
}

static void dump_config_relaxes_the_successors_devmem(void)
{
	struct mkfs fs = { sysfs };
	char err[64];

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\ncmdline = quiet\n"
		     "dump = /var/crash/vmcore\n", &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.cmdline, "quiet iomem=relaxed");
	config_free(&cfg);

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\ndump = /var/crash/vmcore\n",
		     &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.cmdline, "iomem=relaxed");
	config_free(&cfg);

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\n"
		     "cmdline = iomem=relaxed quiet\ndump = /var/crash/vmcore\n",
		     &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.cmdline, "iomem=relaxed quiet");
	config_free(&cfg);
}

static void arm_passes_devices_to_the_instance(void)
{
	struct mkfs fs = { sysfs };
	char err[64];
	void *dtbo;
	size_t len;
	int frag, ov, op, item;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\ndevices = 0000:09:00.0\n",
		     &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@1");
	CHECK(frag >= 0);
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "device-add");
	item = fdt_subnode_offset(dtbo, op, "pci@0");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), "0000:09:00.0");
	free(dtbo);
	config_free(&cfg);
}

static int chosen_host_tree(const void *dtbo)
{
	int frag = fdt_path_offset(dtbo, "/fragment@0");
	int ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	int op = fdt_subnode_offset(dtbo, ov, "instance-create");
	int chosen = fdt_subnode_offset(dtbo, op, "chosen");

	return chosen < 0 ? -1 : fdt_subnode_offset(dtbo, chosen, "multikernel,host-tree");
}

static void arm_hands_over_a_host_tree_from_firmware_and_sysfs(void)
{
	struct mkfs fs = { sysfs };
	const uint8_t apics[] = { 0, 1, 2, 3 };
	static const char iomem[] = "00001000-0009fbff : System RAM\n00100000-7ffdbfff : System RAM\n";
	void *dtbo;
	size_t len;
	int ht, node, dev;

	setup();
	put_madt(apics, 4);
	CHECK_EQ(file_write(iomem_path, iomem, sizeof(iomem) - 1), 0);
	put_pci_device("0000:09:00.0", "0x1af4\n", "0x1041\n");
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	ht = chosen_host_tree(dtbo);
	CHECK(ht >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, ht, "cpus", 3), 3);
	node = fdt_subnode_offset(dtbo, ht, "memory@100000");
	CHECK(node >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, node, "reg", 1), 0x7ffdc000 - 0x100000);
	node = fdt_subnode_offset(dtbo, ht, "devices");
	dev = fdt_first_subnode(dtbo, node);
	CHECK_STREQ(fdt_getprop(dtbo, dev, "pci-id", NULL), "0000:09:00.0");
	free(dtbo);
	config_free(&cfg);
}

static void config_machine_cpus_override_the_madt(void)
{
	struct mkfs fs = { sysfs };
	const uint8_t apics[] = { 0, 1 };
	static const char iomem[] = "00100000-7ffdbfff : System RAM\n";
	char err[64];
	void *dtbo;
	size_t len;
	int ht;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\nmachine_cpus = 0-7\n",
		     &cfg, err, sizeof(err));
	put_madt(apics, 2);
	CHECK_EQ(file_write(iomem_path, iomem, sizeof(iomem) - 1), 0);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	ht = chosen_host_tree(dtbo);
	CHECK(ht >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, ht, "cpus", 7), 7);
	free(dtbo);
	config_free(&cfg);
}

static void put_cpu_note(int n, const char *addr)
{
	char rel[64];

	snprintf(rel, sizeof(rel), "cpu/cpu%d", n);
	mkdir_rel(rel);
	snprintf(rel, sizeof(rel), "cpu/cpu%d/crash_notes", n);
	put(rel, addr, strlen(addr));
	snprintf(rel, sizeof(rel), "cpu/cpu%d/crash_notes_size", n);
	put(rel, "1024\n", 5);
}

static void put_kcore_with_direct_map(void)
{
	unsigned char blob[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr)];
	Elf64_Ehdr eh;
	Elf64_Phdr ph;

	memset(&eh, 0, sizeof(eh));
	memset(&ph, 0, sizeof(ph));
	memcpy(eh.e_ident, ELFMAG, SELFMAG);
	eh.e_ident[EI_CLASS] = ELFCLASS64;
	eh.e_phoff = sizeof(eh);
	eh.e_phentsize = sizeof(ph);
	eh.e_phnum = 1;
	ph.p_type = PT_LOAD;
	ph.p_vaddr = 0xffff888000100000ULL;
	ph.p_paddr = 0x100000;
	memcpy(blob, &eh, sizeof(eh));
	memcpy(blob + sizeof(eh), &ph, sizeof(ph));
	put("kcore", blob, sizeof(blob));
}

static void arm_records_the_crash_layout_in_the_host_tree(void)
{
	struct mkfs fs = { sysfs };
	char err[64];
	void *dtbo;
	size_t len;
	int ht, vm, sub, plen;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12-15\nmemory = 4GB\nkernel = /boot/vmlinuz\ndump = /var/crash/vmcore\n",
		     &cfg, err, sizeof(err));
	put("vmcoreinfo", "0x000000007ffd1000 1000\n", 24);
	put_cpu_note(0, "7fc01000\n");
	put_cpu_note(1, "7fc01400\n");
	put_kcore_with_direct_map();
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	ht = chosen_host_tree(dtbo);
	vm = fdt_subnode_offset(dtbo, ht, "vmcore");
	CHECK(vm >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, vm, "page-offset", 0), 0xffff888000000000ULL);
	sub = fdt_subnode_offset(dtbo, vm, "vmcoreinfo");
	CHECK_EQ(fdt_test_get_u64(dtbo, sub, "reg", 0), 0x7ffd1000);
	sub = fdt_subnode_offset(dtbo, vm, "cpu-notes");
	CHECK(fdt_getprop(dtbo, sub, "reg", &plen) != NULL);
	CHECK_EQ(plen, 32);
	CHECK_EQ(fdt_test_get_u64(dtbo, sub, "reg", 2), 0x7fc01400);
	free(dtbo);
	config_free(&cfg);
}

static void arm_without_a_dump_omits_the_vmcore_node(void)
{
	struct mkfs fs = { sysfs };
	void *dtbo;
	size_t len;
	int ht;

	setup();
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	ht = chosen_host_tree(dtbo);
	CHECK(ht >= 0);
	CHECK(fdt_subnode_offset(dtbo, ht, "vmcore") < 0);
	free(dtbo);
	config_free(&cfg);
}

static void arm_without_crash_support_omits_the_vmcore_node(void)
{
	struct mkfs fs = { sysfs };
	char err[64];
	void *dtbo;
	size_t len;
	int ht;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12-15\nmemory = 4GB\nkernel = /boot/vmlinuz\ndump = /var/crash/vmcore\n",
		     &cfg, err, sizeof(err));
	unlink(vmcoreinfo_path);
	unlink(kcore_path);
	{
		char path[1024];

		snprintf(path, sizeof(path), "%s/cpu0/crash_notes", cpu_root);
		unlink(path);
		snprintf(path, sizeof(path), "%s/cpu1/crash_notes", cpu_root);
		unlink(path);
	}
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	ht = chosen_host_tree(dtbo);
	CHECK(ht >= 0);
	CHECK(fdt_subnode_offset(dtbo, ht, "vmcore") < 0);
	free(dtbo);
	config_free(&cfg);
}

static void arm_refuses_to_proceed_without_a_host_tree(void)
{
	struct mkfs fs = { sysfs };

	setup();
	unlink(madt_path);
	unlink(iomem_path);
	CHECK(arm_run(&cfg, &fs, &hooks) < 0);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void parse_console_config(void)
{
	char text[1024], err[64];

	config_free(&cfg);
	snprintf(text, sizeof(text),
		 "cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\nconsole = ttyS0\nconsole_login = %s\n",
		 login_path);
	CHECK_EQ(config_parse(text, &cfg, err, sizeof(err)), 0);
}

static void put_root_tree_with_serial(void)
{
	void *fdt = fdt_test_begin(2048);
	uint64_t free_cpus[] = { 12, 13, 14, 15 };

	fdt_begin_node(fdt, "resources");
	fdt_test_u64_array(fdt, "cpus", free_cpus, 4);
	fdt_begin_node(fdt, "memory@100000000");
	fdt_property_string(fdt, "device_type", "memory");
	fdt_test_reg(fdt, 0x100000000, 8ULL << 30);
	fdt_end_node(fdt);
	fdt_begin_node(fdt, "devices");
	fdt_begin_node(fdt, "serial8250");
	fdt_property_string(fdt, "device-type", "platform");
	fdt_property_string(fdt, "device-name", "serial8250");
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);
	put("device_tree", fdt, fdt_totalsize(fdt));
	free(fdt);
}

static void console_config_hands_the_serial_device_to_the_successor(void)
{
	struct mkfs fs = { sysfs };
	void *dtbo;
	size_t len;
	int frag, ov, op, res, devs, dev;

	setup();
	put_root_tree_with_serial();
	parse_console_config();
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "instance-create");
	res = fdt_subnode_offset(dtbo, op, "resources");
	devs = fdt_subnode_offset(dtbo, res, "devices");
	CHECK(devs >= 0);
	dev = fdt_subnode_offset(dtbo, devs, "serial8250");
	CHECK(dev >= 0);
	CHECK_STREQ(fdt_getprop(dtbo, dev, "device-name", NULL), "serial8250");
	free(dtbo);
	config_free(&cfg);
}

static void console_config_puts_the_serial_device_in_a_new_pool(void)
{
	struct mkfs fs = { sysfs };
	void *dtb;
	size_t len;
	int res, devs, dev;

	setup();
	parse_console_config();
	put("device_tree", "", 0);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	dtb = read_blob("device_tree", &len);
	res = fdt_path_offset(dtb, "/resources");
	devs = fdt_subnode_offset(dtb, res, "devices");
	CHECK(devs >= 0);
	dev = fdt_subnode_offset(dtb, devs, "serial8250");
	CHECK(dev >= 0);
	CHECK_STREQ(fdt_getprop(dtb, dev, "device-type", NULL), "platform");
	free(dtb);
	config_free(&cfg);
}

static void console_config_refuses_a_pool_without_the_serial_device(void)
{
	struct mkfs fs = { sysfs };

	setup();
	parse_console_config();
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -ENOENT);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void arm_refuses_cpus_outside_the_pool(void)
{
	struct mkfs fs = { sysfs };
	char err[64];

	setup();
	config_free(&cfg);
	config_parse("cpus = 12-16\nmemory = 1GB\nkernel = /boot/vmlinuz\n", &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -ENOENT);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void arm_refuses_more_memory_than_free(void)
{
	struct mkfs fs = { sysfs };

	setup();
	put_root_tree(1ULL << 30);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -ENOSPC);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void arm_initialises_the_pool_when_there_is_none(void)
{
	struct mkfs fs = { sysfs };
	char err[64];
	void *dtb;
	size_t len;
	int res, devs, dev, plen;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12-15\nmemory = 4GB\nkernel = /boot/vmlinuz\ndevices = 0000:09:00.0\n",
		     &cfg, err, sizeof(err));
	put_pci_device("0000:09:00.0", "0x1af4\n", "0x1041\n");
	put("device_tree", "", 0);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.seq, "KLE");
	dtb = read_blob("device_tree", &len);
	CHECK_EQ(fdtutil_check(dtb, len), 0);
	res = fdt_path_offset(dtb, "/resources");
	CHECK(res >= 0);
	CHECK_EQ(fdt_test_get_u64(dtb, res, "cpus", 0), 12);
	/* One memory block of headroom: the pool park area lives in pool memory. */
	CHECK_EQ(fdt_test_get_u64(dtb, fdt_subnode_offset(dtb, res, "memory@0"), "size", 0),
		 (4ULL << 30) + (128ULL << 20));
	devs = fdt_subnode_offset(dtb, res, "devices");
	CHECK(devs >= 0);
	dev = fdt_first_subnode(dtb, devs);
	CHECK_STREQ(fdt_getprop(dtb, dev, "pci-id", NULL), "0000:09:00.0");
	CHECK_EQ(fdt32_to_cpu(*(const fdt32_t *)fdt_getprop(dtb, dev, "vendor-id", &plen)), 0x1af4);
	free(dtb);
	config_free(&cfg);
}

static void arm_refuses_a_pool_baseline_with_an_unknown_device(void)
{
	struct mkfs fs = { sysfs };
	char err[64];

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\ndevices = 0000:0b:00.0\n",
		     &cfg, err, sizeof(err));
	put("device_tree", "", 0);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -ENOENT);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void missing_pool_devices_are_added_by_overlay(void)
{
	struct mkfs fs = { sysfs };
	const char *present[] = { "0000:09:00.0" };
	struct pool_view pool = { { NULL, 0 }, 0, 0, { (char **)present, 1 }, { NULL, 0 } };
	char err[64];
	void *dtbo;
	size_t len;
	int frag, ov, op, item;

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\n"
		     "devices = 0000:09:00.0, 0000:0a:00.0\n", &cfg, err, sizeof(err));
	CHECK_EQ(arm_pool_add_devices(&cfg, &fs, &pool), 1);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/resources");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "device-add");
	item = fdt_subnode_offset(dtbo, op, "pci@0");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), "0000:0a:00.0");
	CHECK(fdt_subnode_offset(dtbo, op, "pci@1") < 0);
	free(dtbo);

	put("overlays/new", "", 0);
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\nkernel = /boot/vmlinuz\ndevices = 0000:09:00.0\n",
		     &cfg, err, sizeof(err));
	CHECK_EQ(arm_pool_add_devices(&cfg, &fs, &pool), 0);
	dtbo = read_blob("overlays/new", &len);
	CHECK_EQ(len, 0);
	free(dtbo);
	config_free(&cfg);
}

static void arm_uses_the_configured_initrd_over_the_default(void)
{
	struct mkfs fs = { sysfs };
	char other[700];
	struct cpio_entry e;

	setup();
	snprintf(other, sizeof(other), "%s/other.img", sysfs);
	CHECK_EQ(file_write(other, "OTHER-IMAGE-BYTES", 17), 0);
	cfg.initrd = strdup(other);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), 0);
	CHECK(calls.initrd_len > 17);
	CHECK(memcmp(calls.initrd, "OTHER-IMAGE-BYTES", 17) == 0);
	{
		size_t off = 17;

		CHECK_EQ(cpio_next(calls.initrd, calls.initrd_len, &off, &e), 1);
		CHECK_STREQ(e.name, "etc");
	}
	config_free(&cfg);
}

static void arm_refuses_a_missing_image_before_creating_the_instance(void)
{
	struct mkfs fs = { sysfs };
	void *dtbo;
	size_t dtbo_len;

	setup();
	unlink(image_path);
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -ENOENT);
	CHECK_STREQ(calls.seq, "");
	dtbo = read_blob("overlays/new", &dtbo_len);
	CHECK_EQ(dtbo_len, 0);
	free(dtbo);
	config_free(&cfg);
}

static void arm_needs_kernel_cpus_and_memory(void)
{
	struct mkfs fs = { sysfs };
	char err[64];

	setup();
	config_free(&cfg);
	config_parse("cpus = 12\nmemory = 1GB\n", &cfg, err, sizeof(err));
	CHECK_EQ(arm_run(&cfg, &fs, &hooks), -EINVAL);
	config_free(&cfg);
}

static void disarm_of_a_loaded_instance_skips_the_halt(void)
{
	struct mkfs fs = { sysfs };

	setup();
	put("instances/successor/status", "loaded\n", 7);
	CHECK_EQ(disarm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.seq, "U");
	config_free(&cfg);
}

static void disarm_of_a_bare_instance_only_removes_it(void)
{
	struct mkfs fs = { sysfs };

	setup();
	put("instances/successor/status", "ready\n", 6);
	CHECK_EQ(disarm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.seq, "");
	config_free(&cfg);
}

static void disarm_halts_then_removes_the_instance(void)
{
	struct mkfs fs = { sysfs };
	void *dtbo;
	size_t len;
	int frag, ov, op;

	setup();
	CHECK_EQ(disarm_run(&cfg, &fs, &hooks), 0);
	CHECK_STREQ(calls.seq, "HU");
	CHECK_EQ(calls.halt_id, 5);
	CHECK_EQ(calls.unloaded_id, 5);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "instance-remove");
	CHECK_STREQ(fdt_getprop(dtbo, op, "instance-name", NULL), "successor");
	free(dtbo);
	config_free(&cfg);
}

TEST_MAIN({
	if (!mkdtemp(sysfs)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(pci_root, sizeof(pci_root), "%s/pci", sysfs);
	mkdir(pci_root, 0755);
	snprintf(block_size, sizeof(block_size), "%s/block_size_bytes", sysfs);
	snprintf(madt_path, sizeof(madt_path), "%s/APIC", sysfs);
	snprintf(iomem_path, sizeof(iomem_path), "%s/iomem", sysfs);
	snprintf(vmcoreinfo_path, sizeof(vmcoreinfo_path), "%s/vmcoreinfo", sysfs);
	snprintf(cpu_root, sizeof(cpu_root), "%s/cpu", sysfs);
	snprintf(kcore_path, sizeof(kcore_path), "%s/kcore", sysfs);
	snprintf(image_path, sizeof(image_path), "%s/successor.img", sysfs);
	snprintf(login_path, sizeof(login_path), "%s/sh", sysfs);
	mkdir(cpu_root, 0755);
	CHECK_EQ(file_write(block_size, "8000000\n", 8), 0);
	RUN(arm_creates_loads_and_execs);
	RUN(dump_config_relaxes_the_successors_devmem);
	RUN(arm_passes_devices_to_the_instance);
	RUN(arm_hands_over_a_host_tree_from_firmware_and_sysfs);
	RUN(config_machine_cpus_override_the_madt);
	RUN(arm_records_the_crash_layout_in_the_host_tree);
	RUN(arm_without_a_dump_omits_the_vmcore_node);
	RUN(arm_without_crash_support_omits_the_vmcore_node);
	RUN(arm_refuses_to_proceed_without_a_host_tree);
	RUN(console_config_hands_the_serial_device_to_the_successor);
	RUN(console_config_puts_the_serial_device_in_a_new_pool);
	RUN(console_config_refuses_a_pool_without_the_serial_device);
	RUN(arm_refuses_cpus_outside_the_pool);
	RUN(arm_refuses_more_memory_than_free);
	RUN(arm_initialises_the_pool_when_there_is_none);
	RUN(arm_refuses_a_pool_baseline_with_an_unknown_device);
	RUN(missing_pool_devices_are_added_by_overlay);
	RUN(arm_uses_the_configured_initrd_over_the_default);
	RUN(arm_refuses_a_missing_image_before_creating_the_instance);
	RUN(arm_needs_kernel_cpus_and_memory);
	RUN(disarm_of_a_loaded_instance_skips_the_halt);
	RUN(disarm_of_a_bare_instance_only_removes_it);
	RUN(disarm_halts_then_removes_the_instance);
})

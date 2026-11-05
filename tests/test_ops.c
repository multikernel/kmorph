#include <elf.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/file.h"
#include "kmorph/fdtutil.h"
#include "ops.h"

static char root[] = "/tmp/kmorph-test-ops-XXXXXX";
static char devtree[512], sysfs[512], iomem[512], mem[512], dumpfile[512], blocksize[512];
static char pci[512];
static int fenced_ids[8], fenced_count;

static int fake_fence(int mk_id)
{
	fenced_ids[fenced_count++] = mk_id;
	return 0;
}

static void put(const char *path, const void *data, size_t len)
{
	CHECK_EQ(file_write(path, data, len), 0);
}

static void put_rel(const char *base, const char *rel, const void *data, size_t len)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", base, rel);
	put(path, data, len);
}

static void mkdir_rel(const char *base, const char *rel)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", base, rel);
	mkdir(path, 0755);
}

static void put_host_device(const char *id, uint32_t class)
{
	char node[64], rel[128], *p;
	fdt32_t cell = cpu_to_fdt32(class);

	snprintf(node, sizeof(node), "chosen/multikernel,host-tree/devices/pci_%s", id);
	for (p = node + strlen("chosen/multikernel,host-tree/devices/"); *p; p++)
		if (*p == ':' || *p == '.')
			*p = '_';
	mkdir_rel(devtree, node);
	snprintf(rel, sizeof(rel), "%s/pci-id", node);
	put_rel(devtree, rel, id, strlen(id) + 1);
	snprintf(rel, sizeof(rel), "%s/class-code", node);
	put_rel(devtree, rel, &cell, sizeof(cell));
}

/*
 * Machine: RAM [0x1000, 0x10000) and [0x100000, 0x300000); the successor
 * owns [0x200000, 0x300000) and CPUs 4,5 of 0..5; control regions at
 * [0x8000, 0x9000). PCI: a root port at 00:01.0, a disk at 04:00.0 and
 * NICs at 05:00.0 and 09:00.0, the last the successor's own.
 */
static void populate(void)
{
	uint64_t own[] = { 4, 5 }, machine[] = { 0, 1, 2, 3, 4, 5 };
	fdt32_t id = cpu_to_fdt32(3);
	static const char iomem_text[] =
		"00001000-0000ffff : Reserved\n"
		"00200000-002fffff : System RAM\n";
	static unsigned char image[0x200000];

	mkdir(devtree, 0755);
	mkdir_rel(devtree, "resources");
	mkdir_rel(devtree, "chosen");
	mkdir_rel(devtree, "chosen/multikernel,host-tree");
	mkdir_rel(devtree, "chosen/multikernel,host-tree/memory@1000");
	mkdir_rel(devtree, "chosen/multikernel,host-tree/memory@100000");
	mkdir_rel(devtree, "chosen/multikernel,host-tree/vmcore");
	mkdir_rel(devtree, "chosen/multikernel,host-tree/vmcore/cpu-notes");
	mkdir_rel(devtree, "chosen/multikernel,host-tree/devices");
	put_host_device("0000:00:01.0", 0x060400);
	put_host_device("0000:04:00.0", 0x010802);
	put_host_device("0000:05:00.0", 0x020000);
	put_host_device("0000:09:00.0", 0x020000);
	mkdir(pci, 0755);
	mkdir_rel(pci, "devices");
	mkdir_rel(pci, "devices/0000:09:00.0");
	put_rel(pci, "drivers_autoprobe", "1", 1);
	put_rel(pci, "drivers_probe", "", 0);
	put_rel(devtree, "model", "successor", 10);
	put_rel(devtree, "id", &id, 4);
	{
		fdt64_t cells[6];
		size_t i;

		for (i = 0; i < 2; i++)
			cells[i] = cpu_to_fdt64(own[i]);
		put_rel(devtree, "resources/cpus", cells, 16);
		cells[0] = cpu_to_fdt64(0x8000);
		cells[1] = cpu_to_fdt64(0x1000);
		put_rel(devtree, "chosen/multikernel,reserved-memory", cells, 16);
		for (i = 0; i < 6; i++)
			cells[i] = cpu_to_fdt64(machine[i]);
		put_rel(devtree, "chosen/multikernel,host-tree/cpus", cells, 48);
		cells[0] = cpu_to_fdt64(0x1000);
		cells[1] = cpu_to_fdt64(0xf000);
		put_rel(devtree, "chosen/multikernel,host-tree/memory@1000/reg", cells, 16);
		cells[0] = cpu_to_fdt64(0x100000);
		cells[1] = cpu_to_fdt64(0x200000);
		put_rel(devtree, "chosen/multikernel,host-tree/memory@100000/reg", cells, 16);
		cells[0] = cpu_to_fdt64(0x1000);
		cells[1] = cpu_to_fdt64(0x400);
		put_rel(devtree, "chosen/multikernel,host-tree/vmcore/cpu-notes/reg", cells, 16);
	}

	mkdir(sysfs, 0755);
	mkdir_rel(sysfs, "overlays");
	put_rel(sysfs, "overlays/new", "", 0);
	mkdir_rel(sysfs, "overlays/tx_1");
	put_rel(sysfs, "overlays/tx_1/status", "applied\n", 8);
	put_rel(sysfs, "device_tree", "", 0);

	put(iomem, iomem_text, sizeof(iomem_text) - 1);
	put(blocksize, "1000\n", 5);
	memset(image, 0x5a, sizeof(image));
	{
		Elf64_Nhdr nh = { 5, 8, NT_PRSTATUS };

		memset(image + 0x1000, 0, 0x400);
		memcpy(image + 0x1000, &nh, sizeof(nh));
		memcpy(image + 0x1000 + 12, "CORE", 5);
		memset(image + 0x1000 + 20, 0xab, 8);
	}
	put(mem, image, sizeof(image));
}

static struct ops_env env;

static void setup_env(const char *dump)
{
	env.devtree_root = devtree;
	env.sysfs_root = sysfs;
	env.iomem_path = iomem;
	env.mem_path = mem;
	env.dump_path = dump;
	env.block_size_path = blocksize;
	env.fence_fn = fake_fence;
	env.pci_bus = pci;
	env.reset_devices = false;
}

static void *read_blob_at(const char *base, const char *rel, size_t *len)
{
	char path[1024];
	void *blob;

	snprintf(path, sizeof(path), "%s/%s", base, rel);
	CHECK_EQ(file_read(path, &blob, len), 0);
	return blob;
}

static void *read_blob(const char *rel, size_t *len)
{
	return read_blob_at(sysfs, rel, len);
}

static void init_reads_identity_and_host_tree(void)
{
	struct ops o;

	setup_env(NULL);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_STREQ(o.self.name, "successor");
	CHECK_EQ(o.self.id, 3);
	CHECK_EQ(o.host.ram.count, 2);
	CHECK_EQ(o.host.cpus.count, 6);
	CHECK_EQ(o.self.reserved.count, 1);
	ops_free(&o);
}

static void init_fails_without_host_tree(void)
{
	struct ops o;
	struct ops_env bad = env;

	bad.devtree_root = "/nonexistent";
	CHECK(ops_init(&o, &bad) < 0);
}

static void fence_targets_the_predecessor(void)
{
	struct ops o;

	setup_env(NULL);
	CHECK_EQ(ops_init(&o, &env), 0);
	fenced_count = 0;
	CHECK_EQ(ops_fence(&o), 0);
	CHECK_EQ(fenced_count, 1);
	CHECK_EQ(fenced_ids[0], 0);
	ops_free(&o);
}

static void adopt_computes_takeable_and_aligns_to_blocks(void)
{
	struct ops o;
	void *dtbo;
	size_t len;
	int frag, ov, op, m;

	setup_env(NULL);
	put(blocksize, "100000\n", 7);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt(&o), 0);
	CHECK_EQ(o.takeable.cpus.count, 4);
	CHECK_EQ(o.takeable.memory.count, 3);
	CHECK_EQ(o.adoptable.count, 1);
	CHECK_EQ(o.adoptable.r[0].base, 0x100000);
	CHECK_EQ(o.adoptable.r[0].size, 0x100000);

	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "cpu-add");
	CHECK(fdt_subnode_offset(dtbo, op, "cpu@3") >= 0);
	CHECK(fdt_subnode_offset(dtbo, op, "cpu@4") < 0);
	op = fdt_subnode_offset(dtbo, ov, "memory-add");
	m = fdt_subnode_offset(dtbo, op, "memory@0");
	CHECK_EQ(fdt_test_get_u64(dtbo, m, "reg", 0), 0x100000);
	CHECK_EQ(fdt_test_get_u64(dtbo, m, "reg", 1), 0x100000);
	CHECK(fdt_subnode_offset(dtbo, op, "memory@1") < 0);
	free(dtbo);
	ops_free(&o);
	put(blocksize, "1000\n", 5);
}

static void adopt_aligns_to_the_memory_granule(void)
{
	void *tree = fdt_test_granule_tree(0x1000);
	struct ops o;
	size_t i;

	setup_env(NULL);
	put(blocksize, "100000\n", 7);
	put_rel(sysfs, "device_tree", tree, fdt_totalsize(tree));
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt(&o), 0);
	CHECK_EQ(o.adoptable.count, o.takeable.memory.count);
	for (i = 0; i < o.adoptable.count; i++) {
		CHECK_EQ(o.adoptable.r[i].base, o.takeable.memory.r[i].base);
		CHECK_EQ(o.adoptable.r[i].size, o.takeable.memory.r[i].size);
	}
	ops_free(&o);
	free(tree);
	put_rel(sysfs, "device_tree", "", 0);
	put(blocksize, "1000\n", 5);
}

static void adopt_without_dump_takes_everything(void)
{
	struct ops o;
	void *dtbo;
	size_t len;
	int frag, ov;

	setup_env(NULL);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt(&o), 0);

	dtbo = read_blob("overlays/new", &len);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/instances/successor");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	CHECK(fdt_subnode_offset(dtbo, ov, "cpu-add") >= 0);
	CHECK(fdt_subnode_offset(dtbo, ov, "memory-add") >= 0);
	CHECK_EQ(o.preserved.count, 0);
	free(dtbo);

	CHECK_EQ(ops_preserve(&o), 0);
	CHECK_EQ(ops_reap(&o), 0);
	ops_free(&o);
}

static void adopt_with_dump_keeps_memory_until_reaped(void)
{
	struct ops o;
	void *dtbo, *data;
	size_t len;
	int frag, ov;

	setup_env(dumpfile);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt(&o), 0);

	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	CHECK(fdt_subnode_offset(dtbo, ov, "cpu-add") >= 0);
	CHECK(fdt_subnode_offset(dtbo, ov, "memory-add") < 0);
	CHECK_EQ(o.preserved.count, 3);
	free(dtbo);

	CHECK_EQ(ops_preserve(&o), 0);
	CHECK_EQ(file_read(dumpfile, &data, &len), 0);
	{
		Elf64_Ehdr *eh = data;
		Elf64_Phdr *ph = (Elf64_Phdr *)((char *)data + eh->e_phoff);

		CHECK_EQ(memcmp(eh->e_ident, ELFMAG, SELFMAG), 0);
		CHECK_EQ(eh->e_phnum, 4);		/* PT_NOTE + 3 takeable ranges */
		CHECK_EQ(ph[0].p_type, PT_NOTE);
		CHECK_EQ(ph[0].p_filesz, 28);		/* one CORE note: 12 + 8 + 8 */
		CHECK_EQ(ph[1].p_paddr, 0x1000);
		CHECK_EQ(ph[3].p_paddr, 0x100000);
		CHECK_EQ(((unsigned char *)data)[ph[3].p_offset], 0x5a);
	}
	free(data);

	CHECK_EQ(ops_reap(&o), 0);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	CHECK(fdt_subnode_offset(dtbo, ov, "cpu-add") < 0);
	CHECK(fdt_subnode_offset(dtbo, ov, "memory-add") >= 0);
	CHECK_EQ(o.preserved.count, 0);
	free(dtbo);
	ops_free(&o);
}

static void failed_dump_keeps_the_memory_unclaimed(void)
{
	struct ops o;
	struct ops_env bad;

	setup_env(dumpfile);
	bad = env;
	bad.mem_path = "/nonexistent/mem";
	CHECK_EQ(ops_init(&o, &bad), 0);
	CHECK_EQ(ops_adopt(&o), 0);
	CHECK_EQ(o.preserved.count, 3);
	CHECK(ops_preserve(&o) < 0);
	CHECK_EQ(o.preserved.count, 3);
	ops_free(&o);
}

/* The PCI id named by the last overlay written, which adds one device. */
static void check_last_device_add(const char *id)
{
	void *dtbo;
	size_t len;
	int frag, ov, op, item;

	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/instances/successor");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "device-add");
	item = fdt_subnode_offset(dtbo, op, "pci@0");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), id);
	free(dtbo);
}

static void adopt_devices_takes_every_function_it_lacks_but_bridges(void)
{
	struct ops o;

	setup_env(NULL);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt_devices(&o), 0);
	CHECK_EQ(o.devices_adopted, 2);
	check_last_device_add("0000:05:00.0");
	ops_free(&o);
}

static void a_device_that_fails_does_not_stop_the_others(void)
{
	struct ops o;

	setup_env(NULL);
	put_rel(sysfs, "overlays/tx_1/status", "failed\n", 7);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK(ops_adopt_devices(&o) < 0);
	CHECK_EQ(o.devices_adopted, 0);
	check_last_device_add("0000:05:00.0");
	ops_free(&o);
	put_rel(sysfs, "overlays/tx_1/status", "applied\n", 8);
}

static void adopt_takes_devices_once_before_cpus_and_memory(void)
{
	struct ops o;
	void *dtbo;
	size_t len;
	int frag, ov;

	setup_env(NULL);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt(&o), 0);
	CHECK_EQ(o.devices_adopted, 2);
	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	CHECK(fdt_subnode_offset(dtbo, ov, "cpu-add") >= 0);
	CHECK(fdt_subnode_offset(dtbo, ov, "device-add") < 0);
	free(dtbo);

	CHECK_EQ(ops_adopt(&o), 0);
	CHECK_EQ(o.devices_adopted, 2);
	ops_free(&o);
}

static char *read_text(const char *base, const char *rel)
{
	char path[1024], *text;

	snprintf(path, sizeof(path), "%s/%s", base, rel);
	CHECK_EQ(file_read_string(path, &text), 0);
	return text;
}

static void remove_rel(const char *base, const char *rel)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s", base, rel);
	CHECK_EQ(remove(path), 0);
}

/*
 * The kernel would create each adopted function under devices; here
 * 04:00.0 can be reset and 05:00.0 cannot, so it is only quiesced.
 */
static void reset_devices_resets_each_before_its_driver_probes(void)
{
	uint8_t config[64] = { [4] = 0x07, [5] = 0x01 }, *after;
	struct ops o;
	size_t len;
	char *text;

	setup_env(NULL);
	env.reset_devices = true;
	put_rel(pci, "drivers_autoprobe", "1", 1);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(o.foreign_count, 2);
	mkdir_rel(pci, "devices/0000:04:00.0");
	put_rel(pci, "devices/0000:04:00.0/reset", "", 0);
	mkdir_rel(pci, "devices/0000:05:00.0");
	put_rel(pci, "devices/0000:05:00.0/config", config, sizeof(config));

	CHECK_EQ(ops_adopt_devices(&o), 0);
	CHECK_EQ(o.devices_adopted, 2);
	text = read_text(pci, "devices/0000:04:00.0/reset");
	CHECK_STREQ(text, "1");
	free(text);
	after = read_blob_at(pci, "devices/0000:05:00.0/config", &len);
	CHECK_EQ(after[4], 0x03);
	CHECK_EQ(after[5], 0x01);
	free(after);
	text = read_text(pci, "drivers_autoprobe");
	CHECK_STREQ(text, "1");
	free(text);
	text = read_text(pci, "drivers_probe");
	CHECK_STREQ(text, "0000:05:00.0");
	free(text);
	ops_free(&o);

	remove_rel(pci, "devices/0000:04:00.0/reset");
	remove_rel(pci, "devices/0000:04:00.0");
	remove_rel(pci, "devices/0000:05:00.0/config");
	remove_rel(pci, "devices/0000:05:00.0");
	put_rel(pci, "drivers_probe", "", 0);
}

static void without_reset_drivers_bind_as_devices_join(void)
{
	struct ops o;
	char *text;

	setup_env(NULL);
	put_rel(pci, "drivers_autoprobe", "untouched", 9);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK_EQ(ops_adopt_devices(&o), 0);
	text = read_text(pci, "drivers_autoprobe");
	CHECK_STREQ(text, "untouched");
	free(text);
	ops_free(&o);
	put_rel(pci, "drivers_autoprobe", "1", 1);
}

#define HOST_IOMMUS "chosen/multikernel,iommus"
#define HOST_IOMMU HOST_IOMMUS "/iommu@fed90000"

static void put_host_iommu(uint64_t base)
{
	fdt64_t reg[2] = { cpu_to_fdt64(base), cpu_to_fdt64(0x1000) };

	mkdir_rel(devtree, HOST_IOMMUS);
	mkdir_rel(devtree, HOST_IOMMU);
	put_rel(devtree, HOST_IOMMU "/reg", reg, sizeof(reg));
}

static void remove_host_iommu(void)
{
	remove_rel(devtree, HOST_IOMMU "/reg");
	remove_rel(devtree, HOST_IOMMU);
	remove_rel(devtree, HOST_IOMMUS);
}

static void check_last_iommu_add(uint64_t base)
{
	void *dtbo;
	size_t len;
	int frag, ov, op, item;

	dtbo = read_blob("overlays/new", &len);
	frag = fdt_path_offset(dtbo, "/fragment@0");
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/instances/successor");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "iommu-add");
	CHECK(op >= 0);
	item = fdt_subnode_offset(dtbo, op, "dmar@0");
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 0), base);
	free(dtbo);
}

static void put_live_function(const char *id)
{
	uint8_t config[64] = { [4] = 0x07, [5] = 0x01 };
	char rel[128];

	snprintf(rel, sizeof(rel), "devices/%s", id);
	mkdir_rel(pci, rel);
	snprintf(rel, sizeof(rel), "devices/%s/config", id);
	put_rel(pci, rel, config, sizeof(config));
}

static void remove_function(const char *id)
{
	char rel[128];

	snprintf(rel, sizeof(rel), "devices/%s/config", id);
	remove_rel(pci, rel);
	snprintf(rel, sizeof(rel), "devices/%s", id);
	remove_rel(pci, rel);
}

/*
 * The dead host's IOMMU would translate the successor's DMA through the
 * host's page tables, so the successor claims it once nothing the host
 * set up can still reach memory: every function adopted and quiesced.
 */
static void quiesced_devices_let_the_successor_claim_the_iommu(void)
{
	struct ops o;
	uint8_t *after;
	size_t len;
	char *text;

	setup_env(NULL);
	put_host_iommu(0xfed90000);
	CHECK_EQ(ops_init(&o, &env), 0);
	put_live_function("0000:04:00.0");
	put_live_function("0000:05:00.0");

	CHECK_EQ(ops_adopt_devices(&o), 0);
	after = read_blob_at(pci, "devices/0000:04:00.0/config", &len);
	CHECK_EQ(after[4], 0x03);
	free(after);
	check_last_iommu_add(0xfed90000);
	text = read_text(pci, "drivers_autoprobe");
	CHECK_STREQ(text, "1");
	free(text);
	text = read_text(pci, "drivers_probe");
	CHECK_STREQ(text, "0000:05:00.0");
	free(text);
	ops_free(&o);

	remove_function("0000:04:00.0");
	remove_function("0000:05:00.0");
	remove_host_iommu();
	put_rel(pci, "drivers_probe", "", 0);
}

static void the_host_keeps_the_iommu_while_a_device_is_not_adopted(void)
{
	struct ops o;
	char *text;

	setup_env(NULL);
	put_host_iommu(0xfed90000);
	put_rel(sysfs, "overlays/tx_1/status", "failed\n", 7);
	CHECK_EQ(ops_init(&o, &env), 0);
	CHECK(ops_adopt_devices(&o) < 0);
	check_last_device_add("0000:05:00.0");
	text = read_text(pci, "drivers_autoprobe");
	CHECK_STREQ(text, "1");
	free(text);
	ops_free(&o);

	put_rel(sysfs, "overlays/tx_1/status", "applied\n", 8);
	remove_host_iommu();
	put_rel(pci, "drivers_probe", "", 0);
}

TEST_MAIN({
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(devtree, sizeof(devtree), "%s/device-tree", root);
	snprintf(sysfs, sizeof(sysfs), "%s/multikernel", root);
	snprintf(iomem, sizeof(iomem), "%s/iomem", root);
	snprintf(mem, sizeof(mem), "%s/mem", root);
	snprintf(dumpfile, sizeof(dumpfile), "%s/dump", root);
	snprintf(blocksize, sizeof(blocksize), "%s/block_size_bytes", root);
	snprintf(pci, sizeof(pci), "%s/pci", root);
	populate();
	RUN(init_reads_identity_and_host_tree);
	RUN(init_fails_without_host_tree);
	RUN(fence_targets_the_predecessor);
	RUN(adopt_computes_takeable_and_aligns_to_blocks);
	RUN(adopt_aligns_to_the_memory_granule);
	RUN(adopt_without_dump_takes_everything);
	RUN(adopt_with_dump_keeps_memory_until_reaped);
	RUN(failed_dump_keeps_the_memory_unclaimed);
	RUN(adopt_devices_takes_every_function_it_lacks_but_bridges);
	RUN(a_device_that_fails_does_not_stop_the_others);
	RUN(adopt_takes_devices_once_before_cpus_and_memory);
	RUN(reset_devices_resets_each_before_its_driver_probes);
	RUN(without_reset_drivers_bind_as_devices_join);
	RUN(quiesced_devices_let_the_successor_claim_the_iommu);
	RUN(the_host_keeps_the_iommu_while_a_device_is_not_adopted);
})

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/crashinfo.h"
#include "kmorph/file.h"
#include "kmorph/fdtutil.h"
#include "kmorph/host_tree.h"

static char dir[] = "/tmp/kmorph-test-host-tree-XXXXXX";

static void emits_cpus_ram_and_devices_under_the_node(void)
{
	uint64_t ids[] = { 0, 1, 2, 3 };
	struct pci_ids devs[] = { { "0000:09:00.0", 0x1af4, 0x1041 } };
	struct host_tree ht = { { ids, 4 }, RANGESET_INIT, { devs, 1 }, VMCORE_INFO_INIT };
	void *fdt = fdt_test_begin(4096);
	int node, mem, devices, dev, plen;

	rangeset_add(&ht.ram, 0x1000, 0x9f000);
	rangeset_add(&ht.ram, 0x100000, 0x7ff00000);
	fdt_begin_node(fdt, HOST_TREE_NODE);
	CHECK_EQ(host_tree_emit(fdt, &ht), 0);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);

	node = fdt_path_offset(fdt, "/" HOST_TREE_NODE);
	CHECK(node >= 0);
	CHECK_EQ(fdt_test_get_u64(fdt, node, "cpus", 3), 3);
	mem = fdt_subnode_offset(fdt, node, "memory@100000");
	CHECK(mem >= 0);
	CHECK_STREQ(fdt_getprop(fdt, mem, "device_type", NULL), "memory");
	CHECK_EQ(fdt_test_get_u64(fdt, mem, "reg", 1), 0x7ff00000);
	devices = fdt_subnode_offset(fdt, node, "devices");
	CHECK(devices >= 0);
	dev = fdt_first_subnode(fdt, devices);
	CHECK_STREQ(fdt_getprop(fdt, dev, "pci-id", NULL), "0000:09:00.0");
	CHECK_EQ(fdt32_to_cpu(*(const fdt32_t *)fdt_getprop(fdt, dev, "vendor-id", &plen)), 0x1af4);
	free(fdt);
	rangeset_free(&ht.ram);
}

static void emit_needs_cpus_and_ram(void)
{
	uint64_t ids[] = { 0 };
	struct host_tree ht = { { ids, 1 }, RANGESET_INIT, { NULL, 0 }, VMCORE_INFO_INIT };
	struct host_tree nocpus = { { NULL, 0 }, RANGESET_INIT, { NULL, 0 }, VMCORE_INFO_INIT };
	void *fdt = fdt_test_begin(1024);

	CHECK(host_tree_emit(fdt, &ht) < 0);
	rangeset_add(&nocpus.ram, 0x1000, 0x1000);
	CHECK(host_tree_emit(fdt, &nocpus) < 0);
	free(fdt);
	rangeset_free(&nocpus.ram);
}

static void put(const char *rel, const void *data, size_t len)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	CHECK_EQ(file_write(path, data, len), 0);
}

static void mkdir_rel(const char *rel)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	mkdir(path, 0755);
}

static void unlink_rel(const char *rel)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	unlink(path);
}

static void reads_cpus_and_ram_back_from_a_directory(void)
{
	uint64_t cpu_ids[] = { 0, 1, 2, 3, 4, 5 };
	fdt64_t cells[6], reg[2];
	struct host_tree ht;
	size_t i;

	for (i = 0; i < 6; i++)
		cells[i] = cpu_to_fdt64(cpu_ids[i]);
	put("cpus", cells, sizeof(cells));
	mkdir_rel("memory@100000");
	reg[0] = cpu_to_fdt64(0x100000); reg[1] = cpu_to_fdt64(0x7ff00000);
	put("memory@100000/reg", reg, sizeof(reg));
	put("memory@100000/device_type", "memory", 7);
	mkdir_rel("memory@1000");
	reg[0] = cpu_to_fdt64(0x1000); reg[1] = cpu_to_fdt64(0x9f000);
	put("memory@1000/reg", reg, sizeof(reg));
	mkdir_rel("devices");
	put("name", "multikernel,host-tree", 22);

	CHECK_EQ(host_tree_read(dir, &ht), 0);
	CHECK_EQ(ht.cpus.count, 6);
	CHECK_EQ(ht.cpus.ids[5], 5);
	CHECK_EQ(ht.ram.count, 2);
	CHECK_EQ(ht.ram.r[0].base, 0x1000);
	CHECK_EQ(ht.ram.r[1].base, 0x100000);
	CHECK_EQ(ht.ram.r[1].size, 0x7ff00000);
	host_tree_free(&ht);
}

static void missing_directory_is_enoent(void)
{
	struct host_tree ht;

	CHECK_EQ(host_tree_read("/nonexistent/host-tree", &ht), -ENOENT);
}

static void emits_the_vmcore_node_when_present(void)
{
	uint64_t ids[] = { 0, 1 };
	struct host_tree ht = { { ids, 2 }, RANGESET_INIT, { NULL, 0 }, VMCORE_INFO_INIT };
	void *fdt = fdt_test_begin(4096);
	int node, vm, sub, len;
	const fdt64_t *cells;

	rangeset_add(&ht.ram, 0x100000, 0x7ff00000);
	ht.vmcore.has_page_offset = true;
	ht.vmcore.page_offset = 0xffff888000000000ULL;
	ht.vmcore.vmcoreinfo.base = 0x7ffd1000;
	ht.vmcore.vmcoreinfo.size = 0x1000;
	rangelist_add(&ht.vmcore.cpu_notes, 0x7fc01000, 1024);
	rangelist_add(&ht.vmcore.cpu_notes, 0, 0);
	rangelist_add(&ht.vmcore.cpu_notes, 0x7fc01800, 1024);

	fdt_begin_node(fdt, HOST_TREE_NODE);
	CHECK_EQ(host_tree_emit(fdt, &ht), 0);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);

	node = fdt_path_offset(fdt, "/" HOST_TREE_NODE);
	vm = fdt_subnode_offset(fdt, node, "vmcore");
	CHECK(vm >= 0);
	CHECK_EQ(fdt_test_get_u64(fdt, vm, "page-offset", 0), 0xffff888000000000ULL);
	sub = fdt_subnode_offset(fdt, vm, "vmcoreinfo");
	CHECK_EQ(fdt_test_get_u64(fdt, sub, "reg", 0), 0x7ffd1000);
	CHECK_EQ(fdt_test_get_u64(fdt, sub, "reg", 1), 0x1000);
	sub = fdt_subnode_offset(fdt, vm, "cpu-notes");
	cells = fdt_getprop(fdt, sub, "reg", &len);
	CHECK_EQ(len, 48);
	CHECK_EQ(fdt64_to_cpu(cells[3]), 0);
	CHECK_EQ(fdt64_to_cpu(cells[4]), 0x7fc01800);
	free(fdt);
	rangeset_free(&ht.ram);
	rangelist_free(&ht.vmcore.cpu_notes);
}

static void omits_the_vmcore_node_when_absent(void)
{
	uint64_t ids[] = { 0 };
	struct host_tree ht = { { ids, 1 }, RANGESET_INIT, { NULL, 0 }, VMCORE_INFO_INIT };
	void *fdt = fdt_test_begin(1024);
	int node;

	rangeset_add(&ht.ram, 0x100000, 0x1000);
	fdt_begin_node(fdt, HOST_TREE_NODE);
	CHECK_EQ(host_tree_emit(fdt, &ht), 0);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);
	node = fdt_path_offset(fdt, "/" HOST_TREE_NODE);
	CHECK(fdt_subnode_offset(fdt, node, "vmcore") < 0);
	free(fdt);
	rangeset_free(&ht.ram);
}

static void put_u64(const char *rel, uint64_t v)
{
	fdt64_t cell = cpu_to_fdt64(v);

	put(rel, &cell, sizeof(cell));
}

static void reads_the_vmcore_node_back_from_a_directory(void)
{
	fdt64_t reg[4];
	struct host_tree ht;

	/* on top of the tree left by reads_cpus_and_ram_back_from_a_directory */
	mkdir_rel("vmcore");
	put_u64("vmcore/page-offset", 0xffff888000000000ULL);
	mkdir_rel("vmcore/vmcoreinfo");
	reg[0] = cpu_to_fdt64(0x7ffd1000); reg[1] = cpu_to_fdt64(0x1000);
	put("vmcore/vmcoreinfo/reg", reg, 16);
	mkdir_rel("vmcore/cpu-notes");
	reg[0] = cpu_to_fdt64(0x7fc01000); reg[1] = cpu_to_fdt64(1024);
	reg[2] = cpu_to_fdt64(0); reg[3] = cpu_to_fdt64(0);
	put("vmcore/cpu-notes/reg", reg, 32);

	CHECK_EQ(host_tree_read(dir, &ht), 0);
	CHECK(ht.vmcore.has_page_offset);
	CHECK_EQ(ht.vmcore.page_offset, 0xffff888000000000ULL);
	CHECK_EQ(ht.vmcore.vmcoreinfo.base, 0x7ffd1000);
	CHECK_EQ(ht.vmcore.cpu_notes.count, 2);
	CHECK_EQ(ht.vmcore.cpu_notes.r[0].size, 1024);
	CHECK_EQ(ht.vmcore.cpu_notes.r[1].size, 0);
	host_tree_free(&ht);

	unlink_rel("vmcore/cpu-notes/reg");
	unlink_rel("vmcore/page-offset");
	CHECK_EQ(host_tree_read(dir, &ht), 0);
	CHECK(!ht.vmcore.has_page_offset);
	CHECK_EQ(ht.vmcore.vmcoreinfo.size, 0x1000);
	CHECK_EQ(ht.vmcore.cpu_notes.count, 0);
	host_tree_free(&ht);

	put("vmcore/vmcoreinfo/reg", reg, 8);
	CHECK_EQ(host_tree_read(dir, &ht), -EINVAL);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(emits_cpus_ram_and_devices_under_the_node);
	RUN(emit_needs_cpus_and_ram);
	RUN(reads_cpus_and_ram_back_from_a_directory);
	RUN(emits_the_vmcore_node_when_present);
	RUN(omits_the_vmcore_node_when_absent);
	RUN(reads_the_vmcore_node_back_from_a_directory);
	RUN(missing_directory_is_enoent);
})

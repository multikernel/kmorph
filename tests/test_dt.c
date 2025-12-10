#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/dt.h"
#include "kmorph/fdtutil.h"

static int overlay_node(const void *fdt, const char *target, const char *path)
{
	int frag, ov;
	const char *tp;

	CHECK_STREQ(fdt_getprop(fdt, 0, "compatible", NULL), "linux,multikernel-overlay");
	frag = fdt_path_offset(fdt, "/fragment@0");
	CHECK(frag >= 0);
	tp = fdt_getprop(fdt, frag, "target-path", NULL);
	CHECK_STREQ(tp, target);
	ov = fdt_subnode_offset(fdt, frag, "__overlay__");
	CHECK(ov >= 0);
	return path ? fdt_subnode_offset(fdt, ov, path) : ov;
}

static void instance_create_carries_cpus_and_memory(void)
{
	uint64_t ids[] = { 12, 13 };
	struct cpulist cpus = { ids, 2 };
	void *dtbo;
	size_t len;
	int op, res;

	CHECK_EQ(dt_build_instance_create("successor", &cpus, 4ULL << 30, NULL, NULL, NULL, &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	op = overlay_node(dtbo, "/instances", "instance-create");
	CHECK(op >= 0);
	CHECK_STREQ(fdt_getprop(dtbo, op, "instance-name", NULL), "successor");
	res = fdt_subnode_offset(dtbo, op, "resources");
	CHECK(res >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, res, "cpus", 0), 12);
	CHECK_EQ(fdt_test_get_u64(dtbo, res, "cpus", 1), 13);
	CHECK_EQ(fdt_test_get_u64(dtbo, res, "memory-bytes", 0), 4ULL << 30);
	free(dtbo);
}

static void instance_create_adds_devices_in_a_second_fragment(void)
{
	uint64_t ids[] = { 12 };
	struct cpulist cpus = { ids, 1 };
	const char *pci[] = { "0000:09:00.0", "0000:0a:00.0" };
	struct strlist devices = { (char **)pci, 2 };
	void *dtbo;
	size_t len;
	int frag, ov, op, item;

	CHECK_EQ(dt_build_instance_create("successor", &cpus, 1ULL << 30, &devices, NULL, NULL, &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	frag = fdt_path_offset(dtbo, "/fragment@1");
	CHECK(frag >= 0);
	CHECK_STREQ(fdt_getprop(dtbo, frag, "target-path", NULL), "/instances/successor");
	ov = fdt_subnode_offset(dtbo, frag, "__overlay__");
	op = fdt_subnode_offset(dtbo, ov, "device-add");
	CHECK(op >= 0);
	item = fdt_subnode_offset(dtbo, op, "pci@0");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), "0000:09:00.0");
	item = fdt_subnode_offset(dtbo, op, "pci@1");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), "0000:0a:00.0");
	free(dtbo);
}

static void instance_create_lists_platform_devices_in_resources(void)
{
	uint64_t ids[] = { 1 };
	struct cpulist cpus = { ids, 1 };
	const char *names[] = { "serial8250" };
	struct strlist platform = { (char **)names, 1 };
	void *dtbo;
	size_t len;
	int op, res, devs, dev;

	CHECK_EQ(dt_build_instance_create("successor", &cpus, 1ULL << 30, NULL, &platform, NULL,
					  &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	op = overlay_node(dtbo, "/instances", "instance-create");
	res = fdt_subnode_offset(dtbo, op, "resources");
	devs = fdt_subnode_offset(dtbo, res, "devices");
	CHECK(devs >= 0);
	dev = fdt_subnode_offset(dtbo, devs, "serial8250");
	CHECK(dev >= 0);
	CHECK_STREQ(fdt_getprop(dtbo, dev, "device-type", NULL), "platform");
	CHECK_STREQ(fdt_getprop(dtbo, dev, "device-name", NULL), "serial8250");
	free(dtbo);
}

static void instance_create_carries_the_host_tree_in_chosen(void)
{
	uint64_t ids[] = { 1 }, machine_ids[] = { 0, 1, 2, 3 };
	struct cpulist cpus = { ids, 1 };
	struct host_tree ht = { { machine_ids, 4 }, RANGESET_INIT, { NULL, 0 } };
	void *dtbo;
	size_t len;
	int op, chosen, node, mem;

	rangeset_add(&ht.ram, 0x100000, 0x7ff00000);
	CHECK_EQ(dt_build_instance_create("successor", &cpus, 1ULL << 30, NULL, NULL, &ht,
					  &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	op = overlay_node(dtbo, "/instances", "instance-create");
	chosen = fdt_subnode_offset(dtbo, op, "chosen");
	CHECK(chosen >= 0);
	CHECK(fdt_first_property_offset(dtbo, chosen) < 0);
	node = fdt_subnode_offset(dtbo, chosen, "multikernel,host-tree");
	CHECK(node >= 0);
	CHECK_EQ(fdt_test_get_u64(dtbo, node, "cpus", 3), 3);
	mem = fdt_subnode_offset(dtbo, node, "memory@100000");
	CHECK_EQ(fdt_test_get_u64(dtbo, mem, "reg", 0), 0x100000);
	free(dtbo);
	rangeset_free(&ht.ram);

	CHECK_EQ(dt_build_instance_create("successor", &cpus, 1ULL << 30, NULL, NULL, NULL,
					  &dtbo, &len), 0);
	op = overlay_node(dtbo, "/instances", "instance-create");
	CHECK(fdt_subnode_offset(dtbo, op, "chosen") < 0);
	free(dtbo);
}

static void instance_remove_names_the_instance(void)
{
	void *dtbo;
	size_t len;
	int op;

	CHECK_EQ(dt_build_instance_remove("successor", &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	op = overlay_node(dtbo, "/instances", "instance-remove");
	CHECK(op >= 0);
	CHECK_STREQ(fdt_getprop(dtbo, op, "instance-name", NULL), "successor");
	free(dtbo);
}

static void adopt_lists_cpus_and_memory_by_reg(void)
{
	uint64_t ids[] = { 0, 1 };
	struct cpulist cpus = { ids, 2 };
	struct rangeset mem = RANGESET_INIT;
	void *dtbo;
	size_t len;
	int ov, op, item;

	rangeset_add(&mem, 0x100000, 0x200000);
	rangeset_add(&mem, 0x50000000, 0x10000000);

	CHECK_EQ(dt_build_adopt("successor", &cpus, &mem, &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	ov = overlay_node(dtbo, "/instances/successor", NULL);

	op = fdt_subnode_offset(dtbo, ov, "cpu-add");
	CHECK(op >= 0);
	item = fdt_subnode_offset(dtbo, op, "cpu@0");
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 0), 0);
	item = fdt_subnode_offset(dtbo, op, "cpu@1");
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 0), 1);

	op = fdt_subnode_offset(dtbo, ov, "memory-add");
	CHECK(op >= 0);
	item = fdt_subnode_offset(dtbo, op, "memory@0");
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 0), 0x100000);
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 1), 0x200000);
	item = fdt_subnode_offset(dtbo, op, "memory@1");
	CHECK_EQ(fdt_test_get_u64(dtbo, item, "reg", 0), 0x50000000);

	free(dtbo);
	rangeset_free(&mem);
}

static void adopt_omits_empty_operations(void)
{
	struct cpulist cpus = { NULL, 0 };
	struct rangeset mem = RANGESET_INIT;
	void *dtbo;
	size_t len;
	int ov;

	rangeset_add(&mem, 0x100000, 0x200000);
	CHECK_EQ(dt_build_adopt("successor", &cpus, &mem, &dtbo, &len), 0);
	ov = overlay_node(dtbo, "/instances/successor", NULL);
	CHECK(fdt_subnode_offset(dtbo, ov, "cpu-add") < 0);
	CHECK(fdt_subnode_offset(dtbo, ov, "memory-add") >= 0);
	free(dtbo);
	rangeset_free(&mem);

	CHECK(dt_build_adopt("successor", &cpus, &mem, &dtbo, &len) < 0);
}

static void pool_baseline_lists_devices_with_ids(void)
{
	uint64_t ids[] = { 12 };
	struct cpulist cpus = { ids, 1 };
	struct pci_ids devs[] = { { "0000:09:00.0", 0x1af4, 0x1041 } };
	struct pci_list devices = { devs, 1 };
	void *dtb;
	size_t len;
	int res, node, dev, plen;

	CHECK_EQ(dt_build_pool_baseline(&cpus, 1ULL << 30, &devices, NULL, &dtb, &len), 0);
	CHECK_EQ(fdtutil_check(dtb, len), 0);
	res = fdt_path_offset(dtb, "/resources");
	node = fdt_subnode_offset(dtb, res, "devices");
	CHECK(node >= 0);
	dev = fdt_first_subnode(dtb, node);
	CHECK(dev >= 0);
	CHECK_STREQ(fdt_getprop(dtb, dev, "device-type", NULL), "pci");
	CHECK_STREQ(fdt_getprop(dtb, dev, "pci-id", NULL), "0000:09:00.0");
	CHECK_EQ(fdt32_to_cpu(*(const fdt32_t *)fdt_getprop(dtb, dev, "vendor-id", &plen)), 0x1af4);
	CHECK_EQ(fdt32_to_cpu(*(const fdt32_t *)fdt_getprop(dtb, dev, "device-id", &plen)), 0x1041);
	free(dtb);
}

static void pool_baseline_lists_platform_devices(void)
{
	uint64_t ids[] = { 12 };
	struct cpulist cpus = { ids, 1 };
	const char *names[] = { "serial8250" };
	struct strlist platform = { (char **)names, 1 };
	void *dtb;
	size_t len;
	int res, node, dev;

	CHECK_EQ(dt_build_pool_baseline(&cpus, 1ULL << 30, NULL, &platform, &dtb, &len), 0);
	res = fdt_path_offset(dtb, "/resources");
	node = fdt_subnode_offset(dtb, res, "devices");
	CHECK(node >= 0);
	dev = fdt_subnode_offset(dtb, node, "serial8250");
	CHECK(dev >= 0);
	CHECK_STREQ(fdt_getprop(dtb, dev, "device-type", NULL), "platform");
	CHECK_STREQ(fdt_getprop(dtb, dev, "device-name", NULL), "serial8250");
	free(dtb);
}

static void pool_device_add_targets_the_pool(void)
{
	const char *pci[] = { "0000:09:00.0" };
	struct strlist devices = { (char **)pci, 1 };
	void *dtbo;
	size_t len;
	int ov, op, item;

	CHECK_EQ(dt_build_pool_device_add(&devices, &dtbo, &len), 0);
	CHECK_EQ(fdtutil_check(dtbo, len), 0);
	ov = overlay_node(dtbo, "/resources", NULL);
	op = fdt_subnode_offset(dtbo, ov, "device-add");
	CHECK(op >= 0);
	item = fdt_subnode_offset(dtbo, op, "pci@0");
	CHECK_STREQ(fdt_getprop(dtbo, item, "pci-id", NULL), "0000:09:00.0");
	free(dtbo);
}

static void pool_baseline_requests_own_resources_by_size(void)
{
	uint64_t ids[] = { 12, 13, 14, 15 };
	struct cpulist cpus = { ids, 4 };
	void *dtb;
	size_t len;
	int res, mem;

	CHECK_EQ(dt_build_pool_baseline(&cpus, 4ULL << 30, NULL, NULL, &dtb, &len), 0);
	CHECK_EQ(fdtutil_check(dtb, len), 0);
	res = fdt_path_offset(dtb, "/resources");
	CHECK(res >= 0);
	CHECK_EQ(fdt_test_get_u64(dtb, res, "cpus", 3), 15);
	mem = fdt_subnode_offset(dtb, res, "memory@0");
	CHECK(mem >= 0);
	CHECK_EQ(fdt_test_get_u64(dtb, mem, "size", 0), 4ULL << 30);
	CHECK(fdt_getprop(dtb, mem, "reg", NULL) == NULL);
	free(dtb);
}

static void foreign_baseline_names_takeable_by_reg(void)
{
	uint64_t ids[] = { 0, 1 };
	struct takeable t = { RANGESET_INIT, { ids, 2 } };
	void *dtb;
	size_t len;
	int res, mem;

	rangeset_add(&t.memory, 0x1000, 0x9f000);
	rangeset_add(&t.memory, 0x100000, 0x3ff00000);

	CHECK_EQ(dt_build_foreign_baseline(&t, &dtb, &len), 0);
	CHECK_EQ(fdtutil_check(dtb, len), 0);
	res = fdt_path_offset(dtb, "/resources");
	CHECK(res >= 0);
	CHECK_EQ(fdt_test_get_u64(dtb, res, "cpus", 1), 1);
	mem = fdt_subnode_offset(dtb, res, "memory@1");
	CHECK(mem >= 0);
	CHECK_EQ(fdt_test_get_u64(dtb, mem, "reg", 0), 0x100000);
	CHECK_EQ(fdt_test_get_u64(dtb, mem, "reg", 1), 0x3ff00000);
	free(dtb);
	rangeset_free(&t.memory);
}

static void *build_root_tree(void)
{
	void *fdt = fdt_test_begin(4096);
	uint64_t free_cpus[] = { 12, 13, 14, 15 };
	uint64_t inst_cpus[] = { 8, 9 };

	fdt_begin_node(fdt, "resources");
	fdt_test_u64_array(fdt, "cpus", free_cpus, 4);
	fdt_begin_node(fdt, "memory@100000000");
	fdt_property_string(fdt, "device_type", "memory");
	fdt_test_reg(fdt, 0x100000000, 8ULL << 30);
	fdt_end_node(fdt);
	/* The pool's PCI devices, as the kernel describes them: a topology. */
	fdt_begin_node(fdt, "devices");
	fdt_begin_node(fdt, "pci@0");
	fdt_property_string(fdt, "compatible", "multikernel,pci-host-bridge");
	fdt_property_u32(fdt, "linux,pci-domain", 0);
	fdt_begin_node(fdt, "pci@3,0");
	fdt_property_string(fdt, "device_type", "pci");
	{
		fdt32_t reg[5] = { cpu_to_fdt32(0 << 16 | 0x18 << 8) };
		fdt_property(fdt, "reg", reg, sizeof(reg));
	}
	fdt_begin_node(fdt, "pci@0,0");
	{
		fdt32_t reg[5] = { cpu_to_fdt32(9 << 16 | 0 << 8) };
		fdt_property(fdt, "reg", reg, sizeof(reg));
	}
	fdt_property_u32(fdt, "vendor-id", 0x1af4);
	fdt_property_u32(fdt, "device-id", 0x1041);
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_begin_node(fdt, "pci@1,2");
	{
		fdt32_t reg[5] = { cpu_to_fdt32(0 << 16 | 0x0a << 8) };
		fdt_property(fdt, "reg", reg, sizeof(reg));
	}
	fdt_property_u32(fdt, "vendor-id", 0x8086);
	fdt_property_u32(fdt, "device-id", 0x1234);
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_begin_node(fdt, "serial8250");
	fdt_property_string(fdt, "device-type", "platform");
	fdt_property_string(fdt, "device-name", "serial8250");
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_end_node(fdt);

	fdt_begin_node(fdt, "instances");
	fdt_begin_node(fdt, "web");
	fdt_property_u32(fdt, "id", 1);
	fdt_begin_node(fdt, "resources");
	fdt_test_u64_array(fdt, "cpus", inst_cpus, 2);
	fdt_property_u64(fdt, "memory-base", 0x100000000);
	fdt_property_u64(fdt, "memory-bytes", 2ULL << 30);
	fdt_end_node(fdt);
	fdt_end_node(fdt);
	fdt_end_node(fdt);

	fdt_test_finish(fdt);
	return fdt;
}

static void pool_view_reports_free_cpus_and_bytes(void)
{
	void *fdt = build_root_tree();
	struct pool_view v;

	CHECK_EQ(dt_read_pool(fdt, fdt_totalsize(fdt), &v), 0);
	CHECK_EQ(v.free_cpus.count, 4);
	CHECK_EQ(v.free_cpus.ids[0], 12);
	CHECK_EQ(v.total_bytes, 8ULL << 30);
	CHECK_EQ(v.free_bytes, 6ULL << 30);
	CHECK_EQ(v.devices.count, 2);
	CHECK_STREQ(v.devices.items[0], "0000:09:00.0");
	CHECK_STREQ(v.devices.items[1], "0000:00:01.2");
	CHECK_EQ(v.platform_devices.count, 1);
	CHECK_STREQ(v.platform_devices.items[0], "serial8250");
	pool_view_free(&v);
	free(fdt);
}

static void pool_view_treats_a_chunkless_tree_as_no_pool(void)
{
	void *fdt = fdt_test_begin(1024);
	uint64_t own[] = { 0 };
	struct pool_view v;

	fdt_begin_node(fdt, "resources");
	fdt_test_u64_array(fdt, "cpus", own, 1);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);
	CHECK_EQ(dt_read_pool(fdt, fdt_totalsize(fdt), &v), -ENOENT);
	free(fdt);
}

static void pool_view_fails_without_resources(void)
{
	void *fdt = fdt_test_begin(1024);
	struct pool_view v;

	fdt_test_finish(fdt);
	CHECK_EQ(dt_read_pool(fdt, fdt_totalsize(fdt), &v), -ENOENT);
	free(fdt);
}

TEST_MAIN({
	RUN(instance_create_carries_cpus_and_memory);
	RUN(instance_create_adds_devices_in_a_second_fragment);
	RUN(instance_create_lists_platform_devices_in_resources);
	RUN(instance_create_carries_the_host_tree_in_chosen);
	RUN(instance_remove_names_the_instance);
	RUN(adopt_lists_cpus_and_memory_by_reg);
	RUN(adopt_omits_empty_operations);
	RUN(pool_baseline_lists_devices_with_ids);
	RUN(pool_baseline_lists_platform_devices);
	RUN(pool_device_add_targets_the_pool);
	RUN(pool_baseline_requests_own_resources_by_size);
	RUN(foreign_baseline_names_takeable_by_reg);
	RUN(pool_view_reports_free_cpus_and_bytes);
	RUN(pool_view_treats_a_chunkless_tree_as_no_pool);
	RUN(pool_view_fails_without_resources);
})

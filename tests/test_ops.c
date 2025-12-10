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

/*
 * Machine: RAM [0x1000, 0x10000) and [0x100000, 0x300000); the successor
 * owns [0x200000, 0x300000) and CPUs 4,5 of 0..5; control regions at
 * [0x8000, 0x9000).
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
}

static void *read_blob(const char *rel, size_t *len)
{
	char path[1024];
	void *blob;

	snprintf(path, sizeof(path), "%s/%s", sysfs, rel);
	CHECK_EQ(file_read(path, &blob, len), 0);
	return blob;
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
	CHECK_EQ(len, 0x200000);
	CHECK_EQ(((unsigned char *)data)[0x1000], 0x5a);
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
	populate();
	RUN(init_reads_identity_and_host_tree);
	RUN(init_fails_without_host_tree);
	RUN(fence_targets_the_predecessor);
	RUN(adopt_computes_takeable_and_aligns_to_blocks);
	RUN(adopt_without_dump_takes_everything);
	RUN(adopt_with_dump_keeps_memory_until_reaped);
})

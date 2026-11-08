#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/file.h"
#include "kmorph/devtree.h"
#include "kmorph/fdtutil.h"

static char root[] = "/tmp/kmorph-test-devtree-XXXXXX";

static void put(const char *rel, const void *data, size_t len)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", root, rel);
	CHECK_EQ(file_write(path, data, len), 0);
}

static void put_u64s(const char *rel, const uint64_t *v, size_t n)
{
	fdt64_t cells[16];
	size_t i;

	for (i = 0; i < n; i++)
		cells[i] = cpu_to_fdt64(v[i]);
	put(rel, cells, n * sizeof(*cells));
}

static void populate_successor_tree(void)
{
	char path[512];
	uint64_t own[] = { 4, 5 };
	uint64_t reserved[] = { 0x7ff00000, 0x100000, 0x200000, 0x1000 };
	fdt32_t id = cpu_to_fdt32(7);
	uint64_t machine[] = { 0, 1, 2, 3, 4, 5 };

	snprintf(path, sizeof(path), "%s/resources", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/chosen", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/chosen/multikernel,host-tree", root);
	mkdir(path, 0755);

	put("model", "generation-3", 13);
	put("id", &id, sizeof(id));
	put_u64s("resources/cpus", own, 2);
	put_u64s("chosen/multikernel,reserved-memory", reserved, 4);
	put_u64s("chosen/multikernel,host-tree/cpus", machine, 6);
}

static void reads_identity_cpus_and_reserved(void)
{
	struct self_info s;

	populate_successor_tree();
	CHECK(devtree_has_host_tree(root));
	CHECK_EQ(devtree_read_self(root, &s), 0);
	CHECK_STREQ(s.name, "generation-3");
	CHECK_EQ(s.id, 7);
	CHECK_EQ(s.own_cpus.count, 2);
	CHECK_EQ(s.own_cpus.ids[1], 5);
	CHECK_EQ(s.reserved.count, 2);
	CHECK_EQ(s.reserved.r[0].base, 0x200000);
	CHECK_EQ(s.reserved.r[1].base, 0x7ff00000);
	CHECK_EQ(s.reserved.r[1].size, 0x100000);
	self_info_free(&s);
}

static void host_tree_absence_is_detected(void)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/chosen/multikernel,host-tree/cpus", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/chosen/multikernel,host-tree", root);
	rmdir(path);
	CHECK(!devtree_has_host_tree(root));
}

static void a_successor_arms_the_next_generation(void)
{
	char name[32];

	populate_successor_tree();
	devtree_successor_name(root, name, sizeof(name));
	CHECK_STREQ(name, "generation-4");
	put("model", "generation-3x", 14);
	devtree_successor_name(root, name, sizeof(name));
	CHECK_STREQ(name, "generation-1");
	put("model", "generation-3", 13);
}

/* A firmware tree's model names the board, not a generation. */
static void a_host_arms_generation_one(void)
{
	char name[32];

	devtree_successor_name(root, name, sizeof(name));
	CHECK_STREQ(name, "generation-1");
	devtree_successor_name("/nonexistent", name, sizeof(name));
	CHECK_STREQ(name, "generation-1");
}

TEST_MAIN({
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(reads_identity_cpus_and_reserved);
	RUN(a_successor_arms_the_next_generation);
	RUN(host_tree_absence_is_detected);
	RUN(a_host_arms_generation_one);
})

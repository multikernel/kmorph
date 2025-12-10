#include "test.h"
#include "kmorph/takeable.h"

static struct host_tree machine(void)
{
	static uint64_t ids[] = { 0, 1, 2, 3, 4, 5 };
	struct host_tree ht = { { ids, 6 }, RANGESET_INIT, { NULL, 0 } };

	rangeset_add(&ht.ram, 0x1000, 0x9f000);
	rangeset_add(&ht.ram, 0x100000, 0x7ff00000);
	return ht;
}

static void takeable_is_ram_minus_own_minus_reserved(void)
{
	struct host_tree ht = machine();
	struct rangeset own_ram = RANGESET_INIT, reserved = RANGESET_INIT;
	uint64_t own_ids[] = { 4, 5 };
	struct cpulist own = { own_ids, 2 };
	struct takeable t;

	rangeset_add(&own_ram, 0x40000000, 0x10000000);
	rangeset_add(&reserved, 0x7ff00000, 0x100000);
	rangeset_add(&reserved, 0x200000, 0x1000);

	CHECK_EQ(takeable_compute(&ht, &own_ram, &reserved, &own, &t), 0);
	CHECK_EQ(t.memory.count, 4);
	CHECK_EQ(t.memory.r[0].base, 0x1000);
	CHECK_EQ(t.memory.r[0].size, 0x9f000);
	CHECK_EQ(t.memory.r[1].base, 0x100000);
	CHECK_EQ(t.memory.r[1].size, 0x100000);
	CHECK_EQ(t.memory.r[2].base, 0x201000);
	CHECK_EQ(t.memory.r[2].size, 0x40000000 - 0x201000);
	CHECK_EQ(t.memory.r[3].base, 0x50000000);
	CHECK_EQ(t.memory.r[3].size, 0x7ff00000ULL - 0x50000000ULL);
	CHECK_EQ(t.cpus.count, 4);
	CHECK_EQ(t.cpus.ids[0], 0);
	CHECK_EQ(t.cpus.ids[3], 3);

	takeable_free(&t);
	rangeset_free(&reserved);
	rangeset_free(&own_ram);
	rangeset_free(&ht.ram);
}

static void takeable_fails_when_nothing_is_left(void)
{
	struct host_tree ht = machine();
	struct rangeset own_ram = RANGESET_INIT, reserved = RANGESET_INIT;
	uint64_t ids[] = { 0, 1, 2, 3, 4, 5 };
	struct cpulist own = { ids, 6 };
	struct takeable t;

	rangeset_add(&own_ram, 0, 1ULL << 40);
	CHECK(takeable_compute(&ht, &own_ram, &reserved, &own, &t) < 0);
	rangeset_free(&own_ram);
	rangeset_free(&ht.ram);
}

TEST_MAIN({
	RUN(takeable_is_ram_minus_own_minus_reserved);
	RUN(takeable_fails_when_nothing_is_left);
})

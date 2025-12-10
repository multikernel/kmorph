#include "test.h"
#include "kmorph/ranges.h"

static void add_keeps_sorted_and_merges_adjacent(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x3000, 0x1000), 0);
	CHECK_EQ(rangeset_add(&s, 0x1000, 0x1000), 0);
	CHECK_EQ(rangeset_add(&s, 0x2000, 0x1000), 0);
	CHECK_EQ(s.count, 1);
	CHECK_EQ(s.r[0].base, 0x1000);
	CHECK_EQ(s.r[0].size, 0x3000);
	rangeset_free(&s);
}

static void add_merges_overlapping(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x1000, 0x2000), 0);
	CHECK_EQ(rangeset_add(&s, 0x2000, 0x2000), 0);
	CHECK_EQ(s.count, 1);
	CHECK_EQ(s.r[0].base, 0x1000);
	CHECK_EQ(s.r[0].size, 0x3000);
	rangeset_free(&s);
}

static void add_keeps_gaps_separate(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x1000, 0x1000), 0);
	CHECK_EQ(rangeset_add(&s, 0x5000, 0x1000), 0);
	CHECK_EQ(s.count, 2);
	CHECK_EQ(rangeset_total(&s), 0x2000);
	rangeset_free(&s);
}

static void subtract_splits_in_the_middle(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x0, 0x10000), 0);
	CHECK_EQ(rangeset_subtract(&s, 0x4000, 0x2000), 0);
	CHECK_EQ(s.count, 2);
	CHECK_EQ(s.r[0].base, 0x0);
	CHECK_EQ(s.r[0].size, 0x4000);
	CHECK_EQ(s.r[1].base, 0x6000);
	CHECK_EQ(s.r[1].size, 0xa000);
	rangeset_free(&s);
}

static void subtract_trims_edges_and_removes_whole(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x1000, 0x1000), 0);
	CHECK_EQ(rangeset_add(&s, 0x4000, 0x4000), 0);
	CHECK_EQ(rangeset_subtract(&s, 0x0, 0x2000), 0);
	CHECK_EQ(rangeset_subtract(&s, 0x7000, 0x2000), 0);
	CHECK_EQ(s.count, 1);
	CHECK_EQ(s.r[0].base, 0x4000);
	CHECK_EQ(s.r[0].size, 0x3000);
	rangeset_free(&s);
}

static void subtract_of_disjoint_is_noop(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x1000, 0x1000), 0);
	CHECK_EQ(rangeset_subtract(&s, 0x9000, 0x1000), 0);
	CHECK_EQ(s.count, 1);
	CHECK_EQ(s.r[0].base, 0x1000);
	rangeset_free(&s);
}

static void contains_checks_full_coverage(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK_EQ(rangeset_add(&s, 0x1000, 0x4000), 0);
	CHECK(rangeset_contains(&s, 0x2000, 0x1000));
	CHECK(rangeset_contains(&s, 0x1000, 0x4000));
	CHECK(!rangeset_contains(&s, 0x4000, 0x2000));
	CHECK(!rangeset_contains(&s, 0x0, 0x1000));
	rangeset_free(&s);
}

static void align_keeps_only_whole_blocks(void)
{
	struct rangeset s = RANGESET_INIT, out = RANGESET_INIT;

	rangeset_add(&s, 0x1000, 0xf000);		/* below one block */
	rangeset_add(&s, 0x180000, 0x300000);		/* 0x200000..0x400000 fits */
	rangeset_add(&s, 0x500000, 0x100000);		/* exactly one block */
	CHECK_EQ(rangeset_align(&s, 0x100000, &out), 0);
	CHECK_EQ(out.count, 2);
	CHECK_EQ(out.r[0].base, 0x200000);
	CHECK_EQ(out.r[0].size, 0x200000);
	CHECK_EQ(out.r[1].base, 0x500000);
	CHECK_EQ(out.r[1].size, 0x100000);
	rangeset_free(&out);
	rangeset_free(&s);
}

static void empty_range_is_rejected(void)
{
	struct rangeset s = RANGESET_INIT;

	CHECK(rangeset_add(&s, 0x1000, 0) < 0);
	CHECK_EQ(s.count, 0);
}

TEST_MAIN({
	RUN(add_keeps_sorted_and_merges_adjacent);
	RUN(add_merges_overlapping);
	RUN(add_keeps_gaps_separate);
	RUN(subtract_splits_in_the_middle);
	RUN(subtract_trims_edges_and_removes_whole);
	RUN(subtract_of_disjoint_is_noop);
	RUN(contains_checks_full_coverage);
	RUN(align_keeps_only_whole_blocks);
	RUN(empty_range_is_rejected);
})

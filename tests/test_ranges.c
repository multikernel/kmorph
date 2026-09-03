#include "test.h"
#include "kmorph/ranges.h"
#include "fdt_helpers.h"
#include "kmorph/fdtutil.h"

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

static void rangelist_keeps_adjacent_entries_apart(void)
{
	struct rangelist l = RANGELIST_INIT;

	CHECK_EQ(rangelist_add(&l, 0x1000, 0x400), 0);
	CHECK_EQ(rangelist_add(&l, 0x1400, 0x400), 0);
	CHECK_EQ(rangelist_add(&l, 0, 0), 0);
	CHECK_EQ(l.count, 3);
	CHECK_EQ(l.r[1].base, 0x1400);
	CHECK_EQ(l.r[2].size, 0);
	rangelist_free(&l);
	CHECK_EQ(l.count, 0);
	CHECK(l.r == NULL);
}

static void regs_round_trip_through_cells(void)
{
	struct rangelist l = RANGELIST_INIT, back = RANGELIST_INIT;
	void *fdt = fdt_test_begin(1024);
	const void *cells;
	int node, len;

	rangelist_add(&l, 0x1000, 0x400);
	rangelist_add(&l, 0x1400, 0x400);
	fdt_begin_node(fdt, "n");
	CHECK_EQ(fdtutil_prop_regs(fdt, &l), 0);
	fdt_end_node(fdt);
	fdt_test_finish(fdt);

	node = fdt_path_offset(fdt, "/n");
	cells = fdt_getprop(fdt, node, "reg", &len);
	CHECK_EQ(len, 32);
	CHECK_EQ(fdtutil_cells_to_rangelist(cells, len, &back), 0);
	CHECK_EQ(back.count, 2);
	CHECK_EQ(back.r[1].base, 0x1400);
	CHECK_EQ(back.r[1].size, 0x400);
	CHECK_EQ(fdtutil_cells_to_rangelist(cells, 24, &back), -EINVAL);
	rangelist_free(&back);
	rangelist_free(&l);
	free(fdt);
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
	RUN(rangelist_keeps_adjacent_entries_apart);
	RUN(regs_round_trip_through_cells);
})

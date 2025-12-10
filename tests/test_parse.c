#include "test.h"
#include "kmorph/parse.h"

static void size_accepts_plain_bytes(void)
{
	uint64_t v = 0;

	CHECK_EQ(parse_size("4096", &v), 0);
	CHECK_EQ(v, 4096);
}

static void size_accepts_binary_suffixes(void)
{
	uint64_t v;

	CHECK_EQ(parse_size("4GB", &v), 0);
	CHECK_EQ(v, 4ULL << 30);
	CHECK_EQ(parse_size("512M", &v), 0);
	CHECK_EQ(v, 512ULL << 20);
	CHECK_EQ(parse_size("16k", &v), 0);
	CHECK_EQ(v, 16ULL << 10);
	CHECK_EQ(parse_size("1T", &v), 0);
	CHECK_EQ(v, 1ULL << 40);
}

static void size_rejects_garbage(void)
{
	uint64_t v;

	CHECK(parse_size("", &v) < 0);
	CHECK(parse_size("abc", &v) < 0);
	CHECK(parse_size("4X", &v) < 0);
	CHECK(parse_size("4GBB", &v) < 0);
	CHECK(parse_size("-4", &v) < 0);
}

static void duration_defaults_to_milliseconds(void)
{
	uint64_t ms;

	CHECK_EQ(parse_duration_ms("250", &ms), 0);
	CHECK_EQ(ms, 250);
}

static void duration_accepts_units(void)
{
	uint64_t ms;

	CHECK_EQ(parse_duration_ms("100ms", &ms), 0);
	CHECK_EQ(ms, 100);
	CHECK_EQ(parse_duration_ms("2s", &ms), 0);
	CHECK_EQ(ms, 2000);
	CHECK_EQ(parse_duration_ms("3m", &ms), 0);
	CHECK_EQ(ms, 180000);
}

static void duration_rejects_garbage(void)
{
	uint64_t ms;

	CHECK(parse_duration_ms("", &ms) < 0);
	CHECK(parse_duration_ms("fast", &ms) < 0);
	CHECK(parse_duration_ms("10h", &ms) < 0);
}

static void cpulist_expands_ranges_and_sorts(void)
{
	struct cpulist l;

	CHECK_EQ(parse_cpulist("20,12-15,13", &l), 0);
	CHECK_EQ(l.count, 5);
	CHECK_EQ(l.ids[0], 12);
	CHECK_EQ(l.ids[1], 13);
	CHECK_EQ(l.ids[2], 14);
	CHECK_EQ(l.ids[3], 15);
	CHECK_EQ(l.ids[4], 20);
	cpulist_free(&l);
}

static void cpulist_rejects_garbage(void)
{
	struct cpulist l;

	CHECK(parse_cpulist("", &l) < 0);
	CHECK(parse_cpulist("5-3", &l) < 0);
	CHECK(parse_cpulist("1,,2", &l) < 0);
	CHECK(parse_cpulist("a", &l) < 0);
}

TEST_MAIN({
	RUN(size_accepts_plain_bytes);
	RUN(size_accepts_binary_suffixes);
	RUN(size_rejects_garbage);
	RUN(duration_defaults_to_milliseconds);
	RUN(duration_accepts_units);
	RUN(duration_rejects_garbage);
	RUN(cpulist_expands_ranges_and_sorts);
	RUN(cpulist_rejects_garbage);
})

#include <unistd.h>

#include "test.h"
#include "kmorph/log.h"

static void lines_carry_ident_and_level(void)
{
	char buf[256];
	int p[2];
	ssize_t n;

	CHECK_EQ(pipe(p), 0);
	log_init("kmorphd", p[1], -1);
	log_info("armed %d cpus", 4);
	log_warn("late");
	log_err("failed: %s", "boom");
	n = read(p[0], buf, sizeof(buf) - 1);
	CHECK(n > 0);
	buf[n > 0 ? n : 0] = '\0';
	CHECK_STREQ(buf, "kmorphd: armed 4 cpus\nkmorphd: warning: late\nkmorphd: error: failed: boom\n");
	close(p[0]);
	close(p[1]);
}

static void kmsg_gets_priority_prefix(void)
{
	char buf[256];
	int p[2];
	ssize_t n;

	CHECK_EQ(pipe(p), 0);
	log_init("kmorphd", -1, p[1]);
	log_err("dead");
	n = read(p[0], buf, sizeof(buf) - 1);
	CHECK(n > 0);
	buf[n > 0 ? n : 0] = '\0';
	CHECK_STREQ(buf, "<3>kmorphd: error: dead\n");
	close(p[0]);
	close(p[1]);
}

TEST_MAIN({
	RUN(lines_carry_ident_and_level);
	RUN(kmsg_gets_priority_prefix);
})

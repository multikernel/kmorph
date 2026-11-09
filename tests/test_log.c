#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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

static void slurp(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);
	ssize_t n = fd >= 0 ? read(fd, buf, size - 1) : -1;

	buf[n > 0 ? n : 0] = '\0';
	if (fd >= 0)
		close(fd);
}

static void touch(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT, 0600);

	CHECK(fd >= 0);
	if (fd >= 0)
		close(fd);
}

static void kmsg_reopened_per_line(void)
{
	char dir[] = "/tmp/kmorph-log-XXXXXX", path[64], kept[64], buf[256];

	CHECK(mkdtemp(dir) != NULL);
	snprintf(path, sizeof(path), "%s/kmsg", dir);
	snprintf(kept, sizeof(kept), "%s/kept", dir);
	touch(path);
	log_init("kmorphd", -1, -1);
	log_open_kmsg(path);
	CHECK_EQ(rename(path, kept), 0);
	touch(path);

	log_info("reopened");
	slurp(path, buf, sizeof(buf));
	CHECK_STREQ(buf, "<6>kmorphd: reopened\n");
	CHECK_EQ(unlink(path), 0);
	log_info("kept");
	slurp(kept, buf, sizeof(buf));
	CHECK_STREQ(buf, "<6>kmorphd: kept\n");

	log_init("kmorphd", -1, -1);
	unlink(kept);
	rmdir(dir);
}

TEST_MAIN({
	RUN(lines_carry_ident_and_level);
	RUN(kmsg_gets_priority_prefix);
	RUN(kmsg_reopened_per_line);
})

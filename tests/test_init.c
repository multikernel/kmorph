#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test.h"
#include "init.h"

static char trace[1024];

static void add(const char *fmt, const char *a, const char *b, unsigned long flags)
{
	char line[256];

	snprintf(line, sizeof(line), fmt, a, b, flags);
	strncat(trace, line, sizeof(trace) - strlen(trace) - 1);
}

static int fake_mount(const char *src, const char *target, const char *type,
		      unsigned long flags, const char *data)
{
	(void)src; (void)data;
	add("mount %s %s %lx;", type, target, flags);
	return 0;
}

static int fake_mkdir(const char *path)
{
	add("mkdir %s%s;", path, "", 0);
	return 0;
}

static int failing_mount(const char *src, const char *target, const char *type,
			 unsigned long flags, const char *data)
{
	(void)src; (void)data; (void)flags;
	add("mount %s %s;", type, target, 0);
	return strcmp(type, "sysfs") ? 0 : -ENODEV;
}

static void every_filesystem_is_mounted_after_its_mount_point(void)
{
	struct init_env env = { fake_mount, fake_mkdir };

	trace[0] = '\0';
	CHECK_EQ(init_mount_filesystems(&env), 0);
	CHECK_STREQ(trace,
		    "mkdir /proc;mount proc /proc e;"
		    "mkdir /sys;mount sysfs /sys e;"
		    "mkdir /dev;mount devtmpfs /dev 2;"
		    "mkdir /run;mount tmpfs /run 6;");
}

static void the_first_failure_is_reported_and_the_rest_still_mounted(void)
{
	struct init_env env = { failing_mount, fake_mkdir };

	trace[0] = '\0';
	CHECK_EQ(init_mount_filesystems(&env), -ENODEV);
	CHECK(strstr(trace, "mount tmpfs /run;") != NULL);
}

static void reaping_collects_exited_children(void)
{
	pid_t pid = fork();

	if (pid == 0)
		_exit(0);
	CHECK(pid > 0);
	usleep(100000);
	init_reap();
	CHECK_EQ(waitpid(-1, NULL, WNOHANG), -1);
	CHECK_EQ(errno, ECHILD);
}

TEST_MAIN({
	RUN(every_filesystem_is_mounted_after_its_mount_point);
	RUN(the_first_failure_is_reported_and_the_rest_still_mounted);
	RUN(reaping_collects_exited_children);
})

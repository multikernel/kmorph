#include <sys/mount.h>

#include "test.h"
#include "standby.h"

static char trace[2048];
static const char *fail_at;
static int fail_err;
static int fork_result;

static int note(const char *fmt, const char *a, const char *b)
{
	char line[256];

	snprintf(line, sizeof(line), fmt, a, b);
	strncat(trace, line, sizeof(trace) - strlen(trace) - 1);
	if (fail_at && !strcmp(line, fail_at))
		return fail_err;
	return 0;
}

static int fake_mount(const char *src, const char *target, const char *type,
		      unsigned long flags, const char *data)
{
	(void)data;
	if (flags & MS_MOVE)
		return note("move %s %s;", src, target);
	if (!strcmp(type, "overlay"))
		CHECK(strstr(data, "lowerdir=/run/kmorph/root/image,"));
	else
		CHECK(flags & MS_RDONLY);
	return note("mount %s %s;", src, target);
}

static int fake_umount(const char *target) { return note("umount %s%s;", target, ""); }
static int fake_mkdir(const char *path) { return note("mkdir %s%s;", path, ""); }
static int fake_copy(const char *from, const char *to) { return note("copy %s %s;", from, to); }
static int fake_chdir(const char *path) { return note("chdir %s%s;", path, ""); }
static int fake_chroot(const char *path) { return note("chroot %s%s;", path, ""); }
static int fake_write(const char *path, const char *text) { return note("write %s %s;", path, text); }
static int fake_fork(void) { note("fork%s%s;", "", ""); return fork_result; }
static int fake_exec(const char *path) { note("exec %s%s;", path, ""); return -ENOENT; }
static void fake_idle(void) { note("idle%s%s;", "", ""); }

static const struct standby_env env = {
	fake_mount, fake_umount, fake_mkdir, fake_copy, fake_chdir, fake_chroot, fake_write,
	fake_fork, fake_exec, fake_idle,
};

static void run(const char *fail, int err)
{
	trace[0] = '\0';
	fail_at = fail;
	fail_err = err;
	fork_result = 1;
}

static void the_image_becomes_the_root_before_its_init_runs(void)
{
	run(NULL, 0);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -ENOENT);
	CHECK_STREQ(trace,
		    "mkdir /run/kmorph;mkdir /run/kmorph/root;mkdir /run/kmorph/root/image;"
		    "mkdir /run/kmorph/root/upper;mkdir /run/kmorph/root/work;mkdir /standby;"
		    "mount /standby.erofs /run/kmorph/root/image;mount overlay /standby;"
		    "copy /etc/kmorph/kmorph.conf /run/kmorph/kmorph.conf;"
		    "copy /etc/kmorph/network /run/kmorph/network;"
		    "move /proc /standby/proc;move /sys /standby/sys;"
		    "move /dev /standby/dev;move /run /standby/run;"
		    "chdir /standby;move . /;chroot .;chdir /;"
		    "write /proc/sys/net/ipv4/ip_nonlocal_bind 1;"
		    "write /proc/sys/net/ipv6/ip_nonlocal_bind 1;"
		    "fork;exec /usr/lib/systemd/systemd;idle;");
}

static void the_child_goes_on_as_kmorphd(void)
{
	run(NULL, 0);
	fork_result = 0;
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), 0);
	CHECK(strstr(trace, "chroot .;"));
	CHECK(!strstr(trace, "exec"));
}

static void without_a_fork_kmorphd_stays_init_in_the_image(void)
{
	run(NULL, 0);
	fork_result = -EAGAIN;
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), 1);
	CHECK(!strstr(trace, "exec"));
}

static void an_image_that_does_not_mount_changes_nothing(void)
{
	run("mount /standby.erofs /run/kmorph/root/image;", -EINVAL);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -EINVAL);
	CHECK(!strstr(trace, "overlay"));
	CHECK(!strstr(trace, "move"));
}

static void a_root_layer_that_does_not_mount_releases_the_image(void)
{
	run("mount overlay /standby;", -EINVAL);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -EINVAL);
	CHECK(strstr(trace, "mount overlay /standby;umount /run/kmorph/root/image;"));
	CHECK(!strstr(trace, "move"));
}

static void a_missing_config_unmounts_the_image(void)
{
	run("copy /etc/kmorph/kmorph.conf /run/kmorph/kmorph.conf;", -ENOENT);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -ENOENT);
	CHECK(strstr(trace, "copy /etc/kmorph/kmorph.conf /run/kmorph/kmorph.conf;"
			    "umount /standby;umount /run/kmorph/root/image;"));
	CHECK(!strstr(trace, "move"));
}

static void an_image_without_a_network_identity_still_switches(void)
{
	run("copy /etc/kmorph/network /run/kmorph/network;", -ENOENT);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -ENOENT);
	CHECK(strstr(trace, "exec /usr/lib/systemd/systemd;"));
}

static void a_failed_move_puts_back_what_was_moved(void)
{
	run("move /dev /standby/dev;", -EBUSY);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -EBUSY);
	CHECK(strstr(trace,
		     "move /dev /standby/dev;"
		     "move /standby/sys /sys;move /standby/proc /proc;"
		     "umount /standby;"));
	CHECK(!strstr(trace, "chroot"));
}

static void a_root_that_cannot_move_puts_every_filesystem_back(void)
{
	run("move . /;", -EINVAL);
	CHECK_EQ(standby_enter(&env, "/etc/kmorph/kmorph.conf"), -EINVAL);
	CHECK(strstr(trace,
		     "move . /;chdir /;"
		     "move /standby/run /run;move /standby/dev /dev;"
		     "move /standby/sys /sys;move /standby/proc /proc;"
		     "umount /standby;"));
	CHECK(!strstr(trace, "exec"));
}

TEST_MAIN({
	RUN(the_image_becomes_the_root_before_its_init_runs);
	RUN(the_child_goes_on_as_kmorphd);
	RUN(without_a_fork_kmorphd_stays_init_in_the_image);
	RUN(an_image_that_does_not_mount_changes_nothing);
	RUN(a_root_layer_that_does_not_mount_releases_the_image);
	RUN(a_missing_config_unmounts_the_image);
	RUN(an_image_without_a_network_identity_still_switches);
	RUN(a_failed_move_puts_back_what_was_moved);
	RUN(a_root_that_cannot_move_puts_every_filesystem_back);
})

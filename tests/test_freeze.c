#include "test.h"
#include "freeze.h"

static const char mountinfo[] =
	"22 1 252:2 / / rw,relatime shared:1 - xfs /dev/vda2 rw,attr2\n"
	"23 22 0:21 / /proc rw,nosuid shared:2 - proc proc rw\n"
	"24 22 252:1 / /boot rw,relatime shared:3 - xfs /dev/vda1 rw\n"
	"25 22 252:2 /srv /mnt/srv\\040copy rw,relatime shared:1 - xfs /dev/vda2 rw\n"
	"26 22 252:3 / /data ro,relatime shared:4 - ext4 /dev/vda3 rw\n"
	"27 22 252:4 / /old rw,relatime shared:5 - ext4 /dev/vda4 ro\n"
	"28 22 0:44 / /host rw,relatime - 9p /host rw,trans=virtio\n"
	"29 22 252:5 / /space\\040here rw - ext4 /dev/vda5 rw\n";

static char trace[512];
static const char *fail_path;

static int record(const char *op, const char *path)
{
	strncat(trace, op, sizeof(trace) - strlen(trace) - 1);
	strncat(trace, path, sizeof(trace) - strlen(trace) - 1);
	strncat(trace, ";", sizeof(trace) - strlen(trace) - 1);
	return fail_path && !strcmp(path, fail_path) ? -EBUSY : 0;
}

static int fake_freeze(const char *path) { return record("freeze ", path); }
static int fake_thaw(const char *path) { return record("thaw ", path); }
static const struct freeze_ops ops = { fake_freeze, fake_thaw };

static void writable_block_filesystems_are_collected_once(void)
{
	struct freeze_set set;

	CHECK_EQ(freeze_collect(mountinfo, &set), 0);
	CHECK_EQ(set.count, 3);
	CHECK_STREQ(set.paths[0], "/");
	CHECK_STREQ(set.paths[1], "/boot");
	CHECK_STREQ(set.paths[2], "/space here");
	freeze_set_free(&set);
}

static void a_failed_freeze_thaws_what_was_frozen(void)
{
	struct freeze_set set;

	CHECK_EQ(freeze_collect(mountinfo, &set), 0);
	trace[0] = '\0';
	fail_path = "/space here";
	CHECK_EQ(freeze_all(&ops, &set), -EBUSY);
	CHECK_STREQ(trace, "freeze /;freeze /boot;freeze /space here;thaw /boot;thaw /;");

	trace[0] = '\0';
	fail_path = NULL;
	CHECK_EQ(freeze_all(&ops, &set), 0);
	thaw_all(&ops, &set);
	CHECK_STREQ(trace, "freeze /;freeze /boot;freeze /space here;"
		    "thaw /space here;thaw /boot;thaw /;");
	freeze_set_free(&set);
}

TEST_MAIN({
	RUN(writable_block_filesystems_are_collected_once);
	RUN(a_failed_freeze_thaws_what_was_frozen);
})

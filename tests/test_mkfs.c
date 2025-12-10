#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/mkfs.h"

static char root[] = "/tmp/kmorph-test-mkfs-XXXXXX";
static struct mkfs fs;

static void put(const char *rel, const char *text)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", root, rel);
	CHECK_EQ(file_write(path, text, strlen(text)), 0);
}

static void mkdir_rel(const char *rel)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", root, rel);
	mkdir(path, 0755);
}

static void apply_overlay_writes_blob_and_reports_latest_tx(void)
{
	char path[512];
	void *back;
	size_t len;
	int tx = -1;

	mkdir_rel("overlays");
	put("overlays/new", "");
	mkdir_rel("overlays/tx_2");
	put("overlays/tx_2/status", "applied\n");
	mkdir_rel("overlays/tx_10");
	put("overlays/tx_10/status", "applied\n");

	CHECK_EQ(mkfs_apply_overlay(&fs, "DTBO", 4, &tx), 0);
	CHECK_EQ(tx, 10);
	snprintf(path, sizeof(path), "%s/overlays/new", root);
	CHECK_EQ(file_read(path, &back, &len), 0);
	CHECK_EQ(len, 4);
	CHECK(memcmp(back, "DTBO", 4) == 0);
	free(back);
}

static void apply_overlay_reports_failed_transaction(void)
{
	int tx = -1;

	mkdir_rel("overlays/tx_11");
	put("overlays/tx_11/status", "failed\n");
	CHECK_EQ(mkfs_apply_overlay(&fs, "DTBO", 4, &tx), -EIO);
	CHECK_EQ(tx, 11);
}

static void apply_overlay_without_interface_is_enoent(void)
{
	struct mkfs none = { "/nonexistent" };
	int tx;

	CHECK_EQ(mkfs_apply_overlay(&none, "DTBO", 4, &tx), -ENOENT);
}

static void baseline_write_lands_in_device_tree(void)
{
	char path[512];
	void *back;
	size_t len;

	put("device_tree", "");
	CHECK_EQ(mkfs_write_baseline(&fs, "DTB", 3), 0);
	snprintf(path, sizeof(path), "%s/device_tree", root);
	CHECK_EQ(file_read(path, &back, &len), 0);
	CHECK_EQ(len, 3);
	free(back);
}

static void instance_id_and_status_are_read(void)
{
	uint32_t id;
	char status[32];

	mkdir_rel("instances");
	mkdir_rel("instances/web");
	put("instances/web/id", "3\n");
	put("instances/web/status", "active\n");
	CHECK_EQ(mkfs_instance_id(&fs, "web", &id), 0);
	CHECK_EQ(id, 3);
	CHECK_EQ(mkfs_instance_status(&fs, "web", status, sizeof(status)), 0);
	CHECK_STREQ(status, "active");
	CHECK_EQ(mkfs_instance_id(&fs, "nope", &id), -ENOENT);
}

static void mount_is_a_noop_when_the_tree_is_present(void)
{
	struct mkfs none = { "/nonexistent/multikernel" };

	put("device_tree", "");
	CHECK_EQ(mkfs_mount(&fs), 0);
	CHECK(mkfs_mount(&none) < 0);
}

TEST_MAIN({
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	fs.root = root;
	RUN(apply_overlay_writes_blob_and_reports_latest_tx);
	RUN(mount_is_a_noop_when_the_tree_is_present);
	RUN(apply_overlay_reports_failed_transaction);
	RUN(apply_overlay_without_interface_is_enoent);
	RUN(baseline_write_lands_in_device_tree);
	RUN(instance_id_and_status_are_read);
})

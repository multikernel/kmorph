#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/pci.h"

static char root[] = "/tmp/kmorph-test-pci-XXXXXX";

static void reads_vendor_and_device_from_sysfs(void)
{
	char path[512];
	struct pci_ids ids;

	snprintf(path, sizeof(path), "%s/0000:09:00.0", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/0000:09:00.0/vendor", root);
	CHECK_EQ(file_write(path, "0x1af4\n", 7), 0);
	snprintf(path, sizeof(path), "%s/0000:09:00.0/device", root);
	CHECK_EQ(file_write(path, "0x1041\n", 7), 0);

	CHECK_EQ(pci_read_ids(root, "0000:09:00.0", &ids), 0);
	CHECK_STREQ(ids.id, "0000:09:00.0");
	CHECK_EQ(ids.vendor, 0x1af4);
	CHECK_EQ(ids.device, 0x1041);
}

static void lists_every_device_in_sysfs_order(void)
{
	char path[512];
	struct pci_list all;

	snprintf(path, sizeof(path), "%s/0000:00:01.2", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/0000:00:01.2/vendor", root);
	CHECK_EQ(file_write(path, "0x8086\n", 7), 0);
	snprintf(path, sizeof(path), "%s/0000:00:01.2/device", root);
	CHECK_EQ(file_write(path, "0x1234\n", 7), 0);
	snprintf(path, sizeof(path), "%s/not-a-device", root);
	mkdir(path, 0755);

	CHECK_EQ(pci_list_all(root, &all), 0);
	CHECK_EQ(all.count, 2);
	CHECK_STREQ(all.devs[0].id, "0000:00:01.2");
	CHECK_EQ(all.devs[0].vendor, 0x8086);
	CHECK_STREQ(all.devs[1].id, "0000:09:00.0");
	CHECK_EQ(all.devs[1].device, 0x1041);
	pci_list_free(&all);
	CHECK_EQ(pci_list_all("/nonexistent/pci", &all), -ENOENT);
}

static void unknown_device_is_enoent(void)
{
	struct pci_ids ids;

	CHECK_EQ(pci_read_ids(root, "0000:0a:00.0", &ids), -ENOENT);
}

static void malformed_id_is_rejected(void)
{
	struct pci_ids ids;

	CHECK(pci_read_ids(root, "../etc", &ids) < 0);
	CHECK(pci_read_ids(root, "09:00.0", &ids) < 0);
}

TEST_MAIN({
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(reads_vendor_and_device_from_sysfs);
	RUN(lists_every_device_in_sysfs_order);
	RUN(unknown_device_is_enoent);
	RUN(malformed_id_is_rejected);
})

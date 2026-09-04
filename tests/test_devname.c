#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/devname.h"

static char root[] = "/tmp/kmorph-test-devname-XXXXXX";
static char class_root[300], usb_root[300];

static void mkdirs(const char *rel)
{
	char path[600], *p;

	snprintf(path, sizeof(path), "%s/%s", root, rel);
	for (p = path + strlen(root) + 1; *p; p++)
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0755);
			*p = '/';
		}
	mkdir(path, 0755);
}

static void link_rel(const char *link, const char *target)
{
	char path[600], to[600];

	snprintf(path, sizeof(path), "%s/%s", root, link);
	snprintf(to, sizeof(to), "%s/%s", root, target);
	CHECK_EQ(symlink(to, path), 0);
}

static void build_tree(void)
{
	mkdirs("class/net");
	mkdirs("class/block");
	mkdirs("class/graphics");
	mkdirs("usb");
	mkdirs("devices/pci0000:00/0000:00:03.0/net/eth9");
	link_rel("class/net/eth9", "devices/pci0000:00/0000:00:03.0/net/eth9");
	mkdirs("devices/pci0000:00/0000:00:1f.2/ata1/host0/target0:0:0/0:0:0:0/block/sda");
	link_rel("class/block/sda", "devices/pci0000:00/0000:00:1f.2/ata1/host0/target0:0:0/0:0:0:0/block/sda");
	mkdirs("devices/pci0000:00/0000:00:1c.0/0000:02:00.0/graphics/fb0");
	link_rel("class/graphics/fb0", "devices/pci0000:00/0000:00:1c.0/0000:02:00.0/graphics/fb0");
	mkdirs("devices/pci0000:00/0000:00:14.0/usb1");
	link_rel("usb/usb1", "devices/pci0000:00/0000:00:14.0/usb1");
	mkdirs("devices/virtual/net/lo");
	link_rel("class/net/lo", "devices/virtual/net/lo");
	mkdirs("devices/pci0000:00/0000:00:04.0/net/twin");
	link_rel("class/net/twin", "devices/pci0000:00/0000:00:04.0/net/twin");
	mkdirs("devices/pci0000:00/0000:00:05.0/block/twin");
	link_rel("class/block/twin", "devices/pci0000:00/0000:00:05.0/block/twin");
}

static void pci_addresses_are_recognised(void)
{
	CHECK(devname_is_pci_id("0000:3d:00.1"));
	CHECK(!devname_is_pci_id("0000:3d:00"));
	CHECK(!devname_is_pci_id("enp61s0f1"));
	CHECK(!devname_is_pci_id("0000:3d:00.1x"));
}

static void names_resolve_to_the_nearest_pci_ancestor(void)
{
	char id[16];

	CHECK_EQ(devname_resolve(class_root, usb_root, "eth9", id, sizeof(id)), 0);
	CHECK_STREQ(id, "0000:00:03.0");
	CHECK_EQ(devname_resolve(class_root, usb_root, "sda", id, sizeof(id)), 0);
	CHECK_STREQ(id, "0000:00:1f.2");
	CHECK_EQ(devname_resolve(class_root, usb_root, "fb0", id, sizeof(id)), 0);
	CHECK_STREQ(id, "0000:02:00.0");
	CHECK_EQ(devname_resolve(class_root, usb_root, "usb1", id, sizeof(id)), 0);
	CHECK_STREQ(id, "0000:00:14.0");
}

static void unknown_ambiguous_and_non_pci_names_are_refused(void)
{
	char id[16];

	CHECK_EQ(devname_resolve(class_root, usb_root, "nosuch", id, sizeof(id)), -ENOENT);
	CHECK_EQ(devname_resolve(class_root, usb_root, "twin", id, sizeof(id)), -EEXIST);
	CHECK_EQ(devname_resolve(class_root, usb_root, "lo", id, sizeof(id)), -ENODEV);
}

TEST_MAIN({
	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(class_root, sizeof(class_root), "%s/class", root);
	snprintf(usb_root, sizeof(usb_root), "%s/usb", root);
	build_tree();
	RUN(pci_addresses_are_recognised);
	RUN(names_resolve_to_the_nearest_pci_ancestor);
	RUN(unknown_ambiguous_and_non_pci_names_are_refused);
})

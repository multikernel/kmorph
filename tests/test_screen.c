#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "fdt_helpers.h"
#include "kmorph/fdtutil.h"
#include "kmorph/file.h"
#include "kmorph/screen.h"

static char root[] = "/tmp/kmorph-test-screen-XXXXXX";

static void path_of(char *out, size_t len, const char *rel)
{
	snprintf(out, len, "%s/%s", root, rel);
}

static void mkdir_rel(const char *rel)
{
	char path[600];

	path_of(path, sizeof(path), rel);
	mkdir(path, 0755);
}

static void put(const char *rel, const void *data, size_t len)
{
	char path[600];

	path_of(path, sizeof(path), rel);
	CHECK_EQ(file_write(path, data, len), 0);
}

static void link_rel(const char *target_rel, const char *rel)
{
	char target[600], path[600];

	path_of(target, sizeof(target), target_rel);
	path_of(path, sizeof(path), rel);
	unlink(path);
	CHECK_EQ(symlink(target, path), 0);
}

static void reset(void)
{
	char cmd[700];

	snprintf(cmd, sizeof(cmd), "rm -rf %s/*", root);
	CHECK_EQ(system(cmd), 0);
	mkdir_rel("graphics");
	mkdir_rel("graphics/fb0");
	mkdir_rel("devices");
	mkdir_rel("drivers");
}

static void put_layout(const char *size, const char *bpp, const char *stride)
{
	put("graphics/fb0/virtual_size", size, strlen(size));
	put("graphics/fb0/bits_per_pixel", bpp, strlen(bpp));
	put("graphics/fb0/stride", stride, strlen(stride));
}

static void put_pci_display(const char *driver, const char *resource)
{
	char rel[128];

	mkdir_rel("devices/0000:02:00.0");
	put("devices/0000:02:00.0/resource", resource, strlen(resource));
	snprintf(rel, sizeof(rel), "drivers/%s", driver);
	mkdir_rel(rel);
	link_rel(rel, "devices/0000:02:00.0/driver");
	link_rel("devices/0000:02:00.0", "graphics/fb0/device");
}

static void ast_shows_the_start_of_its_vram(void)
{
	struct screen s;

	reset();
	put_layout("1024,768\n", "32\n", "4096\n");
	put_pci_display("ast", "0x0000000180000000 0x0000000181ffffff 0x0000000000140204\n"
			"0x0000000182000000 0x000000018203ffff 0x0000000000040200\n");
	CHECK_EQ(screen_read(root, &s), 0);
	CHECK_EQ(s.addr, 0x180000000ULL);
	CHECK_EQ(s.size, 4096ULL * 768);
	CHECK_EQ(s.width, 1024);
	CHECK_EQ(s.height, 768);
	CHECK_EQ(s.stride, 4096);
	CHECK_STREQ(s.format, "x8r8g8b8");
}

static void a_picture_larger_than_vram_is_refused(void)
{
	struct screen s;

	reset();
	put_layout("1920,1080\n", "32\n", "7680\n");
	put_pci_display("ast", "0x0000000180000000 0x00000001800fffff 0x0000000000140204\n");
	CHECK_EQ(screen_read(root, &s), -EINVAL);
}

static void an_unknown_display_driver_is_refused(void)
{
	struct screen s;

	reset();
	put_layout("1024,768\n", "32\n", "4096\n");
	put_pci_display("mystery", "0x0000000180000000 0x0000000181ffffff 0x0000000000140204\n");
	CHECK_EQ(screen_read(root, &s), -EOPNOTSUPP);
}

static void an_unknown_pixel_size_is_refused(void)
{
	struct screen s;

	reset();
	put_layout("1024,768\n", "24\n", "3072\n");
	put_pci_display("ast", "0x0000000180000000 0x0000000181ffffff 0x0000000000140204\n");
	CHECK_EQ(screen_read(root, &s), -EOPNOTSUPP);
}

static void no_framebuffer_is_enoent(void)
{
	struct screen s;

	reset();
	CHECK_EQ(screen_read(root, &s), -ENOENT);
}

/* A successor arming the next one sees the node it booted with */
static void a_simple_framebuffer_is_read_from_its_node(void)
{
	fdt64_t reg[2] = { cpu_to_fdt64(0x180000000ULL), cpu_to_fdt64(0x300000) };
	fdt32_t w = cpu_to_fdt32(1024), h = cpu_to_fdt32(768), st = cpu_to_fdt32(4096);
	struct screen s;

	reset();
	mkdir_rel("devices/simple-framebuffer.0");
	mkdir_rel("of_node");
	put("of_node/compatible", "simple-framebuffer", sizeof("simple-framebuffer"));
	put("of_node/reg", reg, sizeof(reg));
	put("of_node/width", &w, sizeof(w));
	put("of_node/height", &h, sizeof(h));
	put("of_node/stride", &st, sizeof(st));
	put("of_node/format", "x8r8g8b8", sizeof("x8r8g8b8"));
	link_rel("of_node", "devices/simple-framebuffer.0/of_node");
	link_rel("devices/simple-framebuffer.0", "graphics/fb0/device");
	CHECK_EQ(screen_read(root, &s), 0);
	CHECK_EQ(s.addr, 0x180000000ULL);
	CHECK_EQ(s.size, 0x300000);
	CHECK_EQ(s.width, 1024);
	CHECK_EQ(s.height, 768);
	CHECK_EQ(s.stride, 4096);
	CHECK_STREQ(s.format, "x8r8g8b8");
}

static void emit_writes_a_simple_framebuffer(void)
{
	struct screen s = { 0x180000000ULL, 0x300000, 1024, 768, 4096, "x8r8g8b8" };
	void *fdt = fdt_test_begin(1024);
	int node;

	CHECK_EQ(fdt_begin_node(fdt, "framebuffer"), 0);
	CHECK_EQ(screen_emit(fdt, &s), 0);
	CHECK_EQ(fdt_end_node(fdt), 0);
	fdt_test_finish(fdt);
	node = fdt_path_offset(fdt, "/framebuffer");
	CHECK(node >= 0);
	CHECK_EQ(fdt_node_check_compatible(fdt, node, "simple-framebuffer"), 0);
	CHECK_EQ(fdt_test_get_u64(fdt, node, "reg", 0), 0x180000000ULL);
	CHECK_EQ(fdt_test_get_u64(fdt, node, "reg", 1), 0x300000);
	CHECK_STREQ(fdt_getprop(fdt, node, "format", NULL), "x8r8g8b8");
	free(fdt);
}

TEST_MAIN({
	char cmd[700];

	CHECK(mkdtemp(root) != NULL);
	RUN(ast_shows_the_start_of_its_vram);
	RUN(a_picture_larger_than_vram_is_refused);
	RUN(an_unknown_display_driver_is_refused);
	RUN(an_unknown_pixel_size_is_refused);
	RUN(no_framebuffer_is_enoent);
	RUN(a_simple_framebuffer_is_read_from_its_node);
	RUN(emit_writes_a_simple_framebuffer);
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	CHECK_EQ(system(cmd), 0);
})

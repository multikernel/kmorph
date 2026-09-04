#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "cpio_helpers.h"
#include "kmorph/cpio.h"
#include "kmorph/file.h"

static void files_round_trip_with_their_directories(void)
{
	struct cpio c = CPIO_INIT;
	struct cpio_entry e;
	size_t off = 0;

	CHECK_EQ(cpio_add_file(&c, "/etc/kmorph/kmorph.conf", "cpus = 1\n", 9, 0644), 0);
	CHECK_EQ(cpio_add_file(&c, "init", "#!/bin/sh\n", 10, 0755), 0);
	CHECK_EQ(cpio_finish(&c), 0);

	CHECK_EQ(cpio_next(c.data, c.len, &off, &e), 1);
	CHECK_STREQ(e.name, "etc");
	CHECK_EQ(e.mode, S_IFDIR | 0755);
	CHECK_EQ(e.len, 0);
	CHECK_EQ(cpio_next(c.data, c.len, &off, &e), 1);
	CHECK_STREQ(e.name, "etc/kmorph");
	CHECK_EQ(e.mode, S_IFDIR | 0755);
	CHECK_EQ(cpio_next(c.data, c.len, &off, &e), 1);
	CHECK_STREQ(e.name, "etc/kmorph/kmorph.conf");
	CHECK_EQ(e.mode, S_IFREG | 0644);
	CHECK_EQ(e.len, 9);
	CHECK(memcmp(e.data, "cpus = 1\n", 9) == 0);
	CHECK_EQ(cpio_next(c.data, c.len, &off, &e), 1);
	CHECK_STREQ(e.name, "init");
	CHECK_EQ(e.mode, S_IFREG | 0755);
	CHECK_EQ(e.len, 10);
	CHECK_EQ(cpio_next(c.data, c.len, &off, &e), 0);
	CHECK_EQ(off, c.len);
	cpio_free(&c);
}

static void entries_are_padded_to_four_bytes(void)
{
	struct cpio c = CPIO_INIT;

	CHECK_EQ(cpio_add_file(&c, "a", "xyz", 3, 0644), 0);
	/* 110 header + "a\0" = 112, data 3 padded to 4 */
	CHECK_EQ(c.len, 116);
	CHECK_EQ(cpio_add_file(&c, "bb", "", 0, 0644), 0);
	/* 110 + "bb\0" = 113 padded to 116, no data */
	CHECK_EQ(c.len, 116 + 116);
	cpio_free(&c);
}

static void header_fields_are_ascii_hex(void)
{
	struct cpio c = CPIO_INIT;

	CHECK_EQ(cpio_add_file(&c, "a", "xyz", 3, 0644), 0);
	CHECK(memcmp(c.data, "070701", 6) == 0);
	CHECK(memcmp(c.data + 6, "00000001", 8) == 0);		/* ino */
	CHECK(memcmp(c.data + 6 + 8, "000081a4", 8) == 0);		/* S_IFREG | 0644 */
	CHECK(memcmp(c.data + 6 + 8 * 4, "00000001", 8) == 0);	/* nlink */
	CHECK(memcmp(c.data + 6 + 8 * 6, "00000003", 8) == 0);	/* filesize */
	CHECK(memcmp(c.data + 6 + 8 * 11, "00000002", 8) == 0);	/* namesize */
	cpio_free(&c);
}

static void the_cpio_tool_lists_the_archive(void)
{
	struct cpio c = CPIO_INIT;
	char path[] = "/tmp/kmorph-test-cpio-XXXXXX";
	char cmd[256], *listing;
	int fd;

	if (system("cpio --version >/dev/null 2>&1") != 0) {
		fprintf(stderr, "    (cpio tool absent, skipped)\n");
		return;
	}
	CHECK_EQ(cpio_add_file(&c, "/etc/kmorph/kmorph.conf", "cpus = 1\n", 9, 0644), 0);
	CHECK_EQ(cpio_finish(&c), 0);
	fd = mkstemp(path);
	CHECK(fd >= 0);
	close(fd);
	CHECK_EQ(file_write(path, c.data, c.len), 0);
	snprintf(cmd, sizeof(cmd), "cpio -t --quiet < %s > %s.list 2>&1", path, path);
	CHECK_EQ(system(cmd), 0);
	snprintf(cmd, sizeof(cmd), "%s.list", path);
	CHECK_EQ(file_read_string(cmd, &listing), 0);
	CHECK_STREQ(listing, "etc\netc/kmorph\netc/kmorph/kmorph.conf");
	free(listing);
	unlink(cmd);
	unlink(path);
	cpio_free(&c);
}

TEST_MAIN({
	RUN(files_round_trip_with_their_directories);
	RUN(entries_are_padded_to_four_bytes);
	RUN(header_fields_are_ascii_hex);
	RUN(the_cpio_tool_lists_the_archive);
})

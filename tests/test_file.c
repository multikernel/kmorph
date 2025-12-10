#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"

static char dir[] = "/tmp/kmorph-test-file-XXXXXX";

static void write_then_read_roundtrips_binary(void)
{
	char path[256];
	const unsigned char data[] = { 0, 1, 2, 255, 0, 7 };
	unsigned char *buf;
	size_t len;

	snprintf(path, sizeof(path), "%s/blob", dir);
	CHECK_EQ(file_write(path, data, sizeof(data)), 0);
	CHECK_EQ(file_read(path, (void **)&buf, &len), 0);
	CHECK_EQ(len, sizeof(data));
	CHECK(memcmp(buf, data, len) == 0);
	free(buf);
}

static void read_string_terminates_and_strips_trailing_newline(void)
{
	char path[256];
	char *s;

	snprintf(path, sizeof(path), "%s/text", dir);
	CHECK_EQ(file_write(path, "hello\n", 6), 0);
	CHECK_EQ(file_read_string(path, &s), 0);
	CHECK_STREQ(s, "hello");
	free(s);
}

static void missing_file_reports_enoent(void)
{
	char path[256];
	char *s;

	snprintf(path, sizeof(path), "%s/nope", dir);
	CHECK_EQ(file_read_string(path, &s), -ENOENT);
}

static void read_u32_parses_decimal(void)
{
	char path[256];
	uint32_t v;

	snprintf(path, sizeof(path), "%s/num", dir);
	CHECK_EQ(file_write(path, "42\n", 3), 0);
	CHECK_EQ(file_read_u32(path, &v), 0);
	CHECK_EQ(v, 42);
	CHECK_EQ(file_write(path, "x\n", 2), 0);
	CHECK(file_read_u32(path, &v) < 0);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(write_then_read_roundtrips_binary);
	RUN(read_string_terminates_and_strips_trailing_newline);
	RUN(missing_file_reports_enoent);
	RUN(read_u32_parses_decimal);
})

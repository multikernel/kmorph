#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/dump.h"
#include "kmorph/file.h"

static char dir[] = "/tmp/kmorph-test-dump-XXXXXX";

static void copies_ranges_at_their_physical_offsets(void)
{
	char mem[256], out[256];
	unsigned char image[0x3000], back[0x3000];
	struct rangeset ranges = RANGESET_INIT;
	struct stat st;
	void *data;
	size_t len;

	memset(image, 0x11, 0x1000);
	memset(image + 0x1000, 0x22, 0x1000);
	memset(image + 0x2000, 0x33, 0x1000);
	snprintf(mem, sizeof(mem), "%s/mem", dir);
	snprintf(out, sizeof(out), "%s/dump", dir);
	CHECK_EQ(file_write(mem, image, sizeof(image)), 0);

	rangeset_add(&ranges, 0x0, 0x1000);
	rangeset_add(&ranges, 0x2000, 0x1000);
	CHECK_EQ(dump_ranges(mem, &ranges, out), 0);

	CHECK_EQ(file_read(out, &data, &len), 0);
	CHECK_EQ(len, 0x3000);
	memcpy(back, data, len);
	CHECK_EQ(back[0], 0x11);
	CHECK_EQ(back[0x1000], 0);
	CHECK_EQ(back[0x2fff], 0x33);
	CHECK_EQ(stat(out, &st), 0);
	CHECK(st.st_blocks * 512 < 0x3000);
	free(data);
	rangeset_free(&ranges);
}

static void unreadable_source_is_reported(void)
{
	char out[256];
	struct rangeset ranges = RANGESET_INIT;

	snprintf(out, sizeof(out), "%s/dump2", dir);
	rangeset_add(&ranges, 0x0, 0x1000);
	CHECK(dump_ranges("/nonexistent/mem", &ranges, out) < 0);
	rangeset_free(&ranges);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(copies_ranges_at_their_physical_offsets);
	RUN(unreadable_source_is_reported);
})

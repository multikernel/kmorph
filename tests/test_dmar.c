#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/dmar.h"
#include "kmorph/file.h"

static char dir[] = "/tmp/kmorph-test-dmar-XXXXXX";

/* ACPI DMAR: 48-byte header, then remapping structures of type and length. */
static size_t put_header(unsigned char *t)
{
	memset(t, 0, 48);
	memcpy(t, "DMAR", 4);
	return 48;
}

static size_t put_drhd(unsigned char *t, size_t off, uint64_t base, uint8_t size)
{
	memset(t + off, 0, 16);
	t[off] = 0;
	t[off + 2] = 16;
	t[off + 5] = size;
	memcpy(t + off + 8, &base, 8);
	return off + 16;
}

static void finish(unsigned char *t, size_t len)
{
	uint32_t l = len;

	memcpy(t + 4, &l, 4);
}

static void write_table(const unsigned char *t, size_t len, char *path)
{
	snprintf(path, 512, "%s/DMAR", dir);
	CHECK_EQ(file_write(path, t, len), 0);
}

static void lists_each_remapping_unit_with_its_register_size(void)
{
	unsigned char t[256];
	char path[512];
	struct dmar_info dmar = DMAR_INFO_INIT;
	size_t off = put_header(t);

	off = put_drhd(t, off, 0xfed90000, 0);
	memset(t + off, 0, 24);		/* a reserved memory region, skipped */
	t[off] = 1;
	t[off + 2] = 24;
	off += 24;
	off = put_drhd(t, off, 0xfed91000, 2);
	finish(t, off);
	write_table(t, off, path);

	CHECK_EQ(dmar_read(path, &dmar), 0);
	CHECK_EQ(dmar.units.count, 2);
	CHECK_EQ(dmar.units.r[0].base, 0xfed90000);
	CHECK_EQ(dmar.units.r[0].size, 0x1000);
	CHECK_EQ(dmar.units.r[1].base, 0xfed91000);
	CHECK_EQ(dmar.units.r[1].size, 0x4000);
	CHECK_EQ(dmar.len, off);
	CHECK(!memcmp(dmar.table, t, off));
	dmar_info_free(&dmar);
}

static void rejects_bad_tables(void)
{
	unsigned char t[128];
	char path[512];
	struct dmar_info dmar = DMAR_INFO_INIT;
	size_t off = put_header(t);

	memcpy(t, "APIC", 4);
	finish(t, off);
	write_table(t, off, path);
	CHECK_EQ(dmar_read(path, &dmar), -EINVAL);

	off = put_header(t);
	off = put_drhd(t, off, 0xfed90000, 0);
	t[off - 16 + 2] = 64;		/* runs past the table */
	finish(t, off);
	write_table(t, off, path);
	CHECK_EQ(dmar_read(path, &dmar), -EINVAL);
	CHECK_EQ(dmar.units.count, 0);
	CHECK(!dmar.table);
}

static void a_machine_without_the_table_has_no_units(void)
{
	struct dmar_info dmar = DMAR_INFO_INIT;

	CHECK_EQ(dmar_read("/nonexistent/DMAR", &dmar), -ENOENT);
	CHECK_EQ(dmar.units.count, 0);
	CHECK(!dmar.table);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(lists_each_remapping_unit_with_its_register_size);
	RUN(rejects_bad_tables);
	RUN(a_machine_without_the_table_has_no_units);
})

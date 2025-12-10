#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/madt.h"

static char dir[] = "/tmp/kmorph-test-madt-XXXXXX";

/* ACPI MADT: 44-byte header, then typed entries. */
static size_t put_header(unsigned char *t)
{
	memset(t, 0, 44);
	memcpy(t, "APIC", 4);
	return 44;
}

static size_t put_lapic(unsigned char *t, size_t off, uint8_t apic_id, uint32_t flags)
{
	t[off] = 0;
	t[off + 1] = 8;
	t[off + 2] = apic_id;
	t[off + 3] = apic_id;
	memcpy(t + off + 4, &flags, 4);
	return off + 8;
}

static size_t put_x2apic(unsigned char *t, size_t off, uint32_t apic_id, uint32_t flags)
{
	t[off] = 9;
	t[off + 1] = 16;
	t[off + 2] = t[off + 3] = 0;
	memcpy(t + off + 4, &apic_id, 4);
	memcpy(t + off + 8, &flags, 4);
	memcpy(t + off + 12, &apic_id, 4);
	return off + 16;
}

static void finish(unsigned char *t, size_t len)
{
	uint32_t l = len;

	memcpy(t + 4, &l, 4);
}

static void write_table(const char *name, const unsigned char *t, size_t len, char *path)
{
	snprintf(path, 512, "%s/%s", dir, name);
	CHECK_EQ(file_write(path, t, len), 0);
}

static void lists_enabled_and_online_capable_cpus_in_id_order(void)
{
	unsigned char t[256];
	char path[512];
	struct cpulist cpus;
	size_t off = put_header(t);

	off = put_lapic(t, off, 3, 1);		/* enabled */
	off = put_lapic(t, off, 0, 1);
	off = put_lapic(t, off, 7, 0);		/* disabled, not online capable */
	off = put_lapic(t, off, 5, 2);		/* online capable */
	t[off] = 1; t[off + 1] = 12; off += 12;	/* an I/O APIC entry, skipped */
	off = put_x2apic(t, off, 0x102, 1);
	finish(t, off);
	write_table("madt", t, off, path);

	CHECK_EQ(madt_read_cpus(path, &cpus), 0);
	CHECK_EQ(cpus.count, 4);
	CHECK_EQ(cpus.ids[0], 0);
	CHECK_EQ(cpus.ids[1], 3);
	CHECK_EQ(cpus.ids[2], 5);
	CHECK_EQ(cpus.ids[3], 0x102);
	cpulist_free(&cpus);
}

static void rejects_bad_tables(void)
{
	unsigned char t[64];
	char path[512];
	struct cpulist cpus;
	size_t off = put_header(t);

	memcpy(t, "FACP", 4);
	finish(t, off);
	write_table("wrong", t, off, path);
	CHECK(madt_read_cpus(path, &cpus) < 0);

	memcpy(t, "APIC", 4);
	finish(t, off + 100);		/* claims more than the file holds */
	write_table("short", t, off, path);
	CHECK(madt_read_cpus(path, &cpus) < 0);

	CHECK_EQ(madt_read_cpus("/nonexistent/APIC", &cpus), -ENOENT);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(lists_enabled_and_online_capable_cpus_in_id_order);
	RUN(rejects_bad_tables);
})

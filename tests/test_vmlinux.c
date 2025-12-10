#include <stdlib.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/vmlinux.h"

static char dir[] = "/tmp/kmorph-test-vmlinux-XXXXXX";

/* A minimal ELF64 with one program header and 16 bytes of payload,
 * followed by 8 bytes of trailing junk the loader should not see. */
static size_t fake_elf(unsigned char *buf)
{
	memset(buf, 0, 128);
	memcpy(buf, "\x7f" "ELF\x02\x01\x01", 7);
	*(uint64_t *)(buf + 32) = 64;	/* e_phoff */
	*(uint16_t *)(buf + 54) = 56;	/* e_phentsize */
	*(uint16_t *)(buf + 56) = 1;	/* e_phnum */
	*(uint64_t *)(buf + 64 + 8) = 120;	/* p_offset */
	*(uint64_t *)(buf + 64 + 32) = 16;	/* p_filesz */
	memset(buf + 120, 0xaa, 16);
	memset(buf + 136, 0xbb, 8);
	return 144;
}

static void elf_size_stops_at_last_segment(void)
{
	unsigned char elf[160];
	size_t len = fake_elf(elf);

	CHECK_EQ(vmlinux_elf_size(elf, len), 136);
	CHECK_EQ(vmlinux_elf_size("junk", 4), 0);
}

static size_t fake_bzimage(unsigned char *buf, const void *payload, size_t plen)
{
	size_t setup_sects = 4, header = (setup_sects + 1) * 512;

	memset(buf, 0, header);
	buf[0x1f1] = setup_sects;
	memcpy(buf + 0x202, "HdrS", 4);
	*(uint16_t *)(buf + 0x206) = 0x020f;
	*(uint32_t *)(buf + 0x248) = 0;	/* payload_offset */
	*(uint32_t *)(buf + 0x24c) = plen;
	memcpy(buf + header, payload, plen);
	return header + plen;
}

static void bzimage_payload_is_located_by_header_fields(void)
{
	unsigned char img[4096];
	size_t len = fake_bzimage(img, "\x1f\x8b" "gzipdata", 10);
	size_t off, plen;

	CHECK(vmlinux_is_bzimage(img, len));
	CHECK(!vmlinux_is_bzimage("\x7f" "ELF", 4));
	CHECK_EQ(vmlinux_locate_payload(img, len, &off, &plen), 0);
	CHECK_EQ(off, 5 * 512);
	CHECK_EQ(plen, 10);
	CHECK(vmlinux_locate_payload(img, len - 1, &off, &plen) < 0);
}

static void decompressor_is_chosen_by_magic(void)
{
	CHECK_STREQ(vmlinux_decompressor("\x1f\x8b", 2), "gzip");
	CHECK_STREQ(vmlinux_decompressor("\xfd" "7zXZ\x00", 6), "xz");
	CHECK_STREQ(vmlinux_decompressor("\x28\xb5\x2f\xfd", 4), "zstd");
	CHECK_STREQ(vmlinux_decompressor("\x02\x21\x4c\x18", 4), "lz4");
	CHECK_STREQ(vmlinux_decompressor("\x5d\x00", 2), "lzma");
	CHECK(vmlinux_decompressor("\x89LZO", 4) == NULL);
}

static void open_returns_elf_files_as_they_are(void)
{
	unsigned char elf[160], back[160];
	size_t len = fake_elf(elf);
	char path[256];
	int fd;

	snprintf(path, sizeof(path), "%s/vmlinux", dir);
	CHECK_EQ(file_write(path, elf, len), 0);
	fd = vmlinux_open(path);
	CHECK(fd >= 0);
	CHECK_EQ(read(fd, back, sizeof(back)), (ssize_t)len);
	CHECK(memcmp(back, elf, len) == 0);
	close(fd);
}

static void open_extracts_gzipped_bzimage_into_memfd(void)
{
	unsigned char elf[160], img[4096], back[256];
	size_t elf_len = fake_elf(elf), gz_len, img_len;
	char path[256], cmd[512];
	void *gz;
	int fd;

	snprintf(path, sizeof(path), "%s/payload", dir);
	CHECK_EQ(file_write(path, elf, elf_len), 0);
	snprintf(cmd, sizeof(cmd), "gzip -kf %s", path);
	CHECK_EQ(system(cmd), 0);
	snprintf(path, sizeof(path), "%s/payload.gz", dir);
	CHECK_EQ(file_read(path, &gz, &gz_len), 0);

	img_len = fake_bzimage(img, gz, gz_len);
	free(gz);
	snprintf(path, sizeof(path), "%s/vmlinuz", dir);
	CHECK_EQ(file_write(path, img, img_len), 0);

	fd = vmlinux_open(path);
	CHECK(fd >= 0);
	CHECK_EQ(read(fd, back, sizeof(back)), 136);
	CHECK(memcmp(back, elf, 136) == 0);
	close(fd);
}

static void open_rejects_missing_or_bogus_images(void)
{
	char path[256];

	CHECK(vmlinux_open("/nonexistent/vmlinuz") < 0);
	snprintf(path, sizeof(path), "%s/bogus", dir);
	CHECK_EQ(file_write(path, "not a kernel", 12), 0);
	CHECK(vmlinux_open(path) < 0);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(elf_size_stops_at_last_segment);
	RUN(bzimage_payload_is_located_by_header_fields);
	RUN(decompressor_is_chosen_by_magic);
	RUN(open_returns_elf_files_as_they_are);
	RUN(open_extracts_gzipped_bzimage_into_memfd);
	RUN(open_rejects_missing_or_bogus_images);
})

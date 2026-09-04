#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "cpio_helpers.h"
#include "elf_helpers.h"
#include "kmorph/config.h"
#include "kmorph/file.h"
#include "image.h"

static char dir[] = "/tmp/kmorph-test-image-XXXXXX";
static char kmorphd_path[300], login_path[300];
static unsigned char kmorphd_elf[256], login_elf[256];
static size_t kmorphd_len, login_len;

static void put_binaries(int kmorphd_dynamic, int login_dynamic)
{
	kmorphd_len = fake_elf64(kmorphd_elf, kmorphd_dynamic);
	login_len = fake_elf64(login_elf, login_dynamic);
	login_elf[kmorphd_len - 1] = 0x5a;
	CHECK_EQ(file_write(kmorphd_path, kmorphd_elf, kmorphd_len), 0);
	CHECK_EQ(file_write(login_path, login_elf, login_len), 0);
}

static int find(const struct cpio *c, const char *name, struct cpio_entry *e)
{
	size_t off = 0;
	int ret;

	while ((ret = cpio_next(c->data, c->len, &off, e)) == 1)
		if (!strcmp(e->name, name))
			return 1;
	return ret;
}

static void image_holds_kmorphd_as_init(void)
{
	struct kmorph_config cfg;
	struct cpio c = CPIO_INIT;
	struct cpio_entry e;
	char err[64];

	put_binaries(0, 0);
	CHECK_EQ(config_parse("cpus = 1\nmemory = 128MB\nkernel = /boot/vmlinuz\n", &cfg, err, sizeof(err)), 0);
	CHECK_EQ(image_build(&cfg, kmorphd_path, &c), 0);
	CHECK_EQ(find(&c, "init", &e), 1);
	CHECK_EQ(e.mode, S_IFREG | 0755);
	CHECK_EQ(e.len, kmorphd_len);
	CHECK(memcmp(e.data, kmorphd_elf, kmorphd_len) == 0);
	CHECK_EQ(find(&c, login_path + 1, &e), 0);
	cpio_free(&c);
	config_free(&cfg);
}

static void console_adds_the_login_program_at_its_own_path(void)
{
	struct kmorph_config cfg;
	struct cpio c = CPIO_INIT;
	struct cpio_entry e;
	char text[512], err[64];

	put_binaries(0, 0);
	snprintf(text, sizeof(text), "cpus = 1\nmemory = 128MB\nkernel = /boot/vmlinuz\n"
		 "console = ttyS0\nconsole_login = %s\n", login_path);
	CHECK_EQ(config_parse(text, &cfg, err, sizeof(err)), 0);
	CHECK_EQ(image_build(&cfg, kmorphd_path, &c), 0);
	CHECK_EQ(find(&c, login_path + 1, &e), 1);
	CHECK_EQ(e.mode, S_IFREG | 0755);
	CHECK_EQ(e.len, login_len);
	CHECK(memcmp(e.data, login_elf, login_len) == 0);
	cpio_free(&c);
	config_free(&cfg);
}

static void dynamic_binaries_are_refused(void)
{
	struct kmorph_config cfg;
	struct cpio c = CPIO_INIT;
	char text[512], err[64];

	snprintf(text, sizeof(text), "cpus = 1\nmemory = 128MB\nkernel = /boot/vmlinuz\n"
		 "console = ttyS0\nconsole_login = %s\n", login_path);
	CHECK_EQ(config_parse(text, &cfg, err, sizeof(err)), 0);
	put_binaries(1, 0);
	CHECK_EQ(image_build(&cfg, kmorphd_path, &c), -EINVAL);
	put_binaries(0, 1);
	CHECK_EQ(image_build(&cfg, kmorphd_path, &c), -EINVAL);
	cpio_free(&c);
	config_free(&cfg);
}

static void a_missing_binary_is_reported(void)
{
	struct kmorph_config cfg;
	struct cpio c = CPIO_INIT;
	char err[64];

	CHECK_EQ(config_parse("cpus = 1\nmemory = 128MB\nkernel = /boot/vmlinuz\n", &cfg, err, sizeof(err)), 0);
	unlink(kmorphd_path);
	CHECK_EQ(image_build(&cfg, kmorphd_path, &c), -ENOENT);
	cpio_free(&c);
	config_free(&cfg);
}

static void write_creates_the_directory_and_the_file(void)
{
	struct cpio c = CPIO_INIT;
	char path[400];
	void *back;
	size_t len;
	struct stat st;

	CHECK_EQ(cpio_add_file(&c, "init", "x", 1, 0755), 0);
	CHECK_EQ(cpio_finish(&c), 0);
	snprintf(path, sizeof(path), "%s/lib/kmorph/successor.img", dir);
	CHECK_EQ(image_write(&c, path), 0);
	CHECK_EQ(file_read(path, &back, &len), 0);
	CHECK_EQ(len, c.len);
	CHECK(memcmp(back, c.data, len) == 0);
	CHECK_EQ(stat(path, &st), 0);
	CHECK_EQ(st.st_mode & 0777, 0644);
	free(back);
	cpio_free(&c);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(kmorphd_path, sizeof(kmorphd_path), "%s/kmorphd", dir);
	snprintf(login_path, sizeof(login_path), "%s/sh", dir);
	RUN(image_holds_kmorphd_as_init);
	RUN(console_adds_the_login_program_at_its_own_path);
	RUN(dynamic_binaries_are_refused);
	RUN(a_missing_binary_is_reported);
	RUN(write_creates_the_directory_and_the_file);
})

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/elf.h"
#include "kmorph/file.h"
#include "kmorph/log.h"
#include "image.h"

static int add_static(struct cpio *c, const char *what, const char *source, const char *dest)
{
	void *elf;
	size_t len;
	int ret;

	ret = file_read(source, &elf, &len);
	if (ret) {
		log_err("cannot read %s %s: %s", what, source, strerror(-ret));
		return ret;
	}
	if (elf_needs_interp(elf, len) != 0) {
		log_err("%s %s is not a static executable; the successor image has no libc",
			what, source);
		ret = -EINVAL;
	} else
		ret = cpio_add_file(c, dest, elf, len, 0755);
	free(elf);
	return ret;
}

int image_build(const struct kmorph_config *cfg, const char *kmorphd_path, struct cpio *out)
{
	int ret;

	ret = add_static(out, "kmorphd", kmorphd_path, "init");
	if (!ret && cfg->console)
		ret = add_static(out, "console_login", cfg->console_login, cfg->console_login);
	if (!ret)
		ret = cpio_finish(out);
	return ret;
}

static int mkdir_parents(const char *path)
{
	char dir[PATH_MAX], *p;

	if (strlen(path) >= sizeof(dir))
		return -ENAMETOOLONG;
	strcpy(dir, path);
	for (p = strchr(dir + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = '\0';
		if (mkdir(dir, 0755) < 0 && errno != EEXIST)
			return -errno;
		*p = '/';
	}
	return 0;
}

int image_write(const struct cpio *c, const char *path)
{
	char tmp[PATH_MAX];
	int ret;

	ret = mkdir_parents(path);
	if (ret)
		return ret;
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -ENAMETOOLONG;
	ret = file_write(tmp, c->data, c->len);
	if (!ret && rename(tmp, path) < 0)
		ret = -errno;
	if (ret)
		unlink(tmp);
	return ret;
}

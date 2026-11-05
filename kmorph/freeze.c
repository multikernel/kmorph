#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kmorph/log.h"
#include "freeze.h"

/* musl ships no linux/fs.h */
#ifndef FIFREEZE
#define FIFREEZE _IOWR('X', 119, int)
#define FITHAW _IOWR('X', 120, int)
#endif

static int fs_ioctl(const char *path, unsigned long req)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	int ret = 0;

	if (fd < 0)
		return -errno;
	if (ioctl(fd, req, 0) < 0)
		ret = -errno;
	close(fd);
	return ret;
}

static int sys_freeze(const char *path) { return fs_ioctl(path, FIFREEZE); }
static int sys_thaw(const char *path) { return fs_ioctl(path, FITHAW); }

const struct freeze_ops freeze_default_ops = { sys_freeze, sys_thaw };

static bool has_option(const char *opts, const char *opt)
{
	size_t n = strlen(opt);
	const char *p;

	for (p = opts; p; p = strchr(p, ',')) {
		if (*p == ',')
			p++;
		if (!strncmp(p, opt, n) && (p[n] == ',' || p[n] == '\0'))
			return true;
	}
	return false;
}

/* Mount points escape space, tab, newline and backslash as \ooo. */
static char *unescape(const char *s)
{
	char *out = malloc(strlen(s) + 1), *o = out;

	if (!out)
		return NULL;
	while (*s) {
		if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] && s[3]) {
			*o++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
			s += 4;
		} else {
			*o++ = *s++;
		}
	}
	*o = '\0';
	return out;
}

int freeze_collect(const char *mountinfo, struct freeze_set *set)
{
	char devs[FREEZE_MAX][16], line[4096];
	const char *p = mountinfo;

	memset(set, 0, sizeof(*set));
	while (*p) {
		char dev[16], point[1024], opts[512], fstype[64], source[512], super[512];
		size_t len = strcspn(p, "\n"), i;
		const char *sep;

		snprintf(line, sizeof(line), "%.*s", (int)len, p);
		p += len + (p[len] == '\n');
		sep = strstr(line, " - ");
		if (!sep || sscanf(line, "%*s %*s %15s %*s %1023s %511s", dev, point, opts) != 3 ||
		    sscanf(sep + 3, "%63s %511s %511s", fstype, source, super) != 3)
			continue;
		if (strncmp(source, "/dev/", 5) || has_option(opts, "ro") || has_option(super, "ro"))
			continue;
		for (i = 0; i < set->count; i++)
			if (!strcmp(devs[i], dev))
				break;
		if (i < set->count)
			continue;
		if (set->count == FREEZE_MAX)
			return -E2BIG;
		set->paths[set->count] = unescape(point);
		if (!set->paths[set->count])
			return -ENOMEM;
		snprintf(devs[set->count++], sizeof(devs[0]), "%s", dev);
	}
	return 0;
}

void thaw_all(const struct freeze_ops *ops, const struct freeze_set *set)
{
	size_t i = set->count;

	while (i--) {
		int ret = ops->thaw(set->paths[i]);

		if (ret && ret != -EINVAL)
			log_warn("cannot thaw %s: %s", set->paths[i], strerror(-ret));
	}
}

int freeze_all(const struct freeze_ops *ops, const struct freeze_set *set)
{
	size_t i;

	for (i = 0; i < set->count; i++) {
		int ret = ops->freeze(set->paths[i]);

		if (ret == -EOPNOTSUPP)
			continue;
		if (ret) {
			struct freeze_set done = *set;

			log_err("cannot freeze %s: %s", set->paths[i], strerror(-ret));
			done.count = i;
			thaw_all(ops, &done);
			return ret;
		}
	}
	return 0;
}

void freeze_set_free(struct freeze_set *set)
{
	size_t i;

	for (i = 0; i < set->count; i++)
		free(set->paths[i]);
	memset(set, 0, sizeof(*set));
}

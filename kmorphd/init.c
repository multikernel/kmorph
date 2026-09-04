#include <errno.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "init.h"

static int sys_mount(const char *src, const char *target, const char *type,
		     unsigned long flags, const char *data)
{
	return mount(src, target, type, flags, data) < 0 ? -errno : 0;
}

static int sys_mkdir(const char *path)
{
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		return -errno;
	return 0;
}

const struct init_env init_default_env = { sys_mount, sys_mkdir };

int init_mount_filesystems(const struct init_env *env)
{
	static const struct {
		const char *type, *target, *data;
		unsigned long flags;
	} fs[] = {
		{ "proc", "/proc", NULL, MS_NOSUID | MS_NODEV | MS_NOEXEC },
		{ "sysfs", "/sys", NULL, MS_NOSUID | MS_NODEV | MS_NOEXEC },
		{ "devtmpfs", "/dev", NULL, MS_NOSUID },
		{ "tmpfs", "/run", "mode=755", MS_NOSUID | MS_NODEV },
	};
	int first = 0;
	size_t i;

	for (i = 0; i < sizeof(fs) / sizeof(fs[0]); i++) {
		int ret = env->mkdir(fs[i].target);

		if (!ret)
			ret = env->mount(fs[i].type, fs[i].target, fs[i].type,
					 fs[i].flags, fs[i].data);
		if (ret && !first)
			first = ret;
	}
	return first;
}

void init_reap(void)
{
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

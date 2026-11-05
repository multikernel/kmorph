#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/log.h"
#include "kmorph/netsnap.h"
#include "standby.h"

static const char *const pseudo_fs[] = { "/proc", "/sys", "/dev", "/run" };
#define N_PSEUDO (sizeof(pseudo_fs) / sizeof(pseudo_fs[0]))

static int sys_mount(const char *src, const char *target, const char *type,
		     unsigned long flags, const char *data)
{
	return mount(src, target, type, flags, data) < 0 ? -errno : 0;
}

static int sys_umount(const char *target)
{
	return umount(target) < 0 ? -errno : 0;
}

static int sys_mkdir(const char *path)
{
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		return -errno;
	return 0;
}

static int sys_copy(const char *from, const char *to)
{
	void *buf;
	size_t len;
	int ret;

	ret = file_read(from, &buf, &len);
	if (ret)
		return ret;
	ret = file_write(to, buf, len);
	free(buf);
	return ret;
}

static int sys_chdir(const char *path)
{
	return chdir(path) < 0 ? -errno : 0;
}

static int sys_chroot(const char *path)
{
	return chroot(path) < 0 ? -errno : 0;
}

static int sys_exec(const char *path)
{
	char *argv[] = { (char *)path, NULL };

	execv(path, argv);
	return -errno;
}

const struct standby_env standby_default_env = {
	sys_mount, sys_umount, sys_mkdir, sys_copy, sys_chdir, sys_chroot, sys_exec,
};

static void moved_path(char *buf, size_t len, const char *fs)
{
	snprintf(buf, len, STANDBY_ROOT "%s", fs);
}

static void move_back(const struct standby_env *env, size_t moved)
{
	char path[64];

	while (moved--) {
		moved_path(path, sizeof(path), pseudo_fs[moved]);
		env->mount(path, pseudo_fs[moved], NULL, MS_MOVE, NULL);
	}
}

static int move_pseudo_fs(const struct standby_env *env)
{
	char path[64];
	size_t i;
	int ret;

	for (i = 0; i < N_PSEUDO; i++) {
		moved_path(path, sizeof(path), pseudo_fs[i]);
		ret = env->mount(pseudo_fs[i], path, NULL, MS_MOVE, NULL);
		if (ret) {
			log_err("standby: cannot move %s into the image: %s",
				pseudo_fs[i], strerror(-ret));
			move_back(env, i);
			return ret;
		}
	}
	return 0;
}

/* The image's root must be a mount of its own, not rootfs, for the takeover's pivot_root. */
static int make_root(const struct standby_env *env)
{
	int ret = env->chdir(STANDBY_ROOT);

	if (!ret)
		ret = env->mount(".", "/", NULL, MS_MOVE, NULL);
	if (ret) {
		env->chdir("/");
		return ret;
	}
	ret = env->chroot(".");
	if (!ret)
		ret = env->chdir("/");
	return ret;
}

int standby_enter(const struct standby_env *env, const char *config_path)
{
	int ret;

	ret = env->mkdir(STANDBY_ROOT);
	if (!ret)
		ret = env->mount(STANDBY_IMAGE, STANDBY_ROOT, "erofs", MS_RDONLY, NULL);
	if (ret) {
		log_err("standby: cannot mount %s: %s", STANDBY_IMAGE, strerror(-ret));
		return ret;
	}

	ret = env->mkdir("/run/kmorph");
	if (!ret)
		ret = env->copy(config_path, STANDBY_CONFIG);
	if (ret) {
		log_err("standby: cannot copy %s: %s", config_path, strerror(-ret));
		goto unmount;
	}
	/* Only the predecessor's arm hands over a network identity; an image may carry none. */
	ret = env->copy(NETSNAP_PATH, STANDBY_NETWORK);
	if (ret && ret != -ENOENT)
		log_warn("standby: cannot copy %s: %s", NETSNAP_PATH, strerror(-ret));

	ret = move_pseudo_fs(env);
	if (ret)
		goto unmount;
	ret = make_root(env);
	if (ret) {
		log_err("standby: cannot make the image the root: %s", strerror(-ret));
		move_back(env, N_PSEUDO);
		goto unmount;
	}

	log_info("standby: running %s from the standby image", STANDBY_INIT);
	ret = env->exec(STANDBY_INIT);
	log_err("standby: cannot execute %s: %s", STANDBY_INIT, strerror(-ret));
	return ret;

unmount:
	env->umount(STANDBY_ROOT);
	return ret;
}

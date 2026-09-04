#ifndef KMORPHD_INIT_H
#define KMORPHD_INIT_H

/*
 * kmorphd is the successor's init when it is the image's /init: nothing
 * else mounts the pseudo filesystems, reaps orphans, or stays alive as
 * PID 1.
 */
struct init_env {
	int (*mount)(const char *src, const char *target, const char *type,
		     unsigned long flags, const char *data);
	int (*mkdir)(const char *path);
};

extern const struct init_env init_default_env;

/* Mounts proc, sysfs, devtmpfs and a tmpfs on /run; returns the first error. */
int init_mount_filesystems(const struct init_env *env);
void init_reap(void);

#endif

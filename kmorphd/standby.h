#ifndef KMORPHD_STANDBY_H
#define KMORPHD_STANDBY_H

/*
 * A standby image carries a root filesystem the successor runs before
 * the takeover, so that chosen services are already up when it happens.
 */
#define STANDBY_IMAGE	"/standby.erofs"
#define STANDBY_ROOT	"/standby"
/* Under /run, which moves into the root with them */
#define STANDBY_LAYERS	"/run/kmorph/root"
#define STANDBY_INIT	"/sbin/init"
#define STANDBY_CONFIG	"/run/kmorph/kmorph.conf"
#define STANDBY_NETWORK	"/run/kmorph/network"

struct standby_env {
	int (*mount)(const char *src, const char *target, const char *type,
		     unsigned long flags, const char *data);
	int (*umount)(const char *target);
	int (*mkdir)(const char *path);
	int (*copy)(const char *from, const char *to);
	int (*chdir)(const char *path);
	int (*chroot)(const char *path);
	int (*write)(const char *path, const char *text);
	int (*fork)(void);
	int (*exec)(const char *path);
	/* Never returns but in tests: what PID 1 does when its init cannot run */
	void (*idle)(void);
};

extern const struct standby_env standby_default_env;

/*
 * Mounts the image under a writable layer, moves the pseudo filesystems
 * into it, makes it the root and forks: PID 1 executes the image's init,
 * the child returns 0 to go on as kmorphd, reading STANDBY_CONFIG. 1
 * when the fork failed and the caller stays init in the image; -errno
 * with everything put back when the image did not become the root.
 */
int standby_enter(const struct standby_env *env, const char *config_path);

#endif

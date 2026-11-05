#ifndef KMORPHD_STANDBY_H
#define KMORPHD_STANDBY_H

/*
 * A standby image carries a root filesystem the successor runs before
 * the takeover, so that chosen services are already up when it happens.
 */
#define STANDBY_IMAGE	"/standby.erofs"
#define STANDBY_ROOT	"/standby"
#define STANDBY_INIT	"/usr/lib/systemd/systemd"
#define STANDBY_CONFIG	"/run/kmorph/kmorph.conf"
#define STANDBY_SWITCH_UNIT "kmorph-switch.service"

struct standby_env {
	int (*mount)(const char *src, const char *target, const char *type,
		     unsigned long flags, const char *data);
	int (*umount)(const char *target);
	int (*mkdir)(const char *path);
	int (*copy)(const char *from, const char *to);
	int (*chdir)(const char *path);
	int (*chroot)(const char *path);
	int (*exec)(const char *path);
};

extern const struct standby_env standby_default_env;

/*
 * Mounts the image, moves the pseudo filesystems into it, makes it the
 * root and executes its init. Returns only on failure, with everything
 * it had moved put back, so the caller can go on as the image's init.
 */
int standby_enter(const struct standby_env *env, const char *config_path);

/* Asks the standby system to move onto the machine's root. */
int standby_switch(void);

#endif

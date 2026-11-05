#ifndef KMORPH_FREEZE_H
#define KMORPH_FREEZE_H

#include <stddef.h>

#define FREEZE_MAX 64

/*
 * The writable filesystems on block devices, each once, in mount order:
 * what an upgrade freezes so the successor mounts them clean.
 */
struct freeze_set {
	char *paths[FREEZE_MAX];
	size_t count;
};

struct freeze_ops {
	int (*freeze)(const char *path);
	int (*thaw)(const char *path);
};

extern const struct freeze_ops freeze_default_ops;

int freeze_collect(const char *mountinfo, struct freeze_set *set);
/* Freezes all or, on a failure, thaws those it froze and returns the error. */
int freeze_all(const struct freeze_ops *ops, const struct freeze_set *set);
void thaw_all(const struct freeze_ops *ops, const struct freeze_set *set);
void freeze_set_free(struct freeze_set *set);

#endif

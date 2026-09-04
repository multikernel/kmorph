#ifndef KMORPH_IMAGE_H
#define KMORPH_IMAGE_H

#include "kmorph/config.h"
#include "kmorph/cpio.h"

/*
 * The successor image: an initramfs holding the static kmorphd as /init
 * and, for the console, the login program at its own path. The image
 * carries no libc, so both must be static executables. The config is
 * not in it; arm appends that when it loads the image.
 */
int image_build(const struct kmorph_config *cfg, const char *kmorphd_path, struct cpio *out);

/* Writes the archive to path atomically, creating the directory. */
int image_write(const struct cpio *c, const char *path);

#endif

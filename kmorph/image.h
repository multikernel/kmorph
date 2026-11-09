#ifndef KMORPH_IMAGE_H
#define KMORPH_IMAGE_H

#include <stddef.h>
#include <stdio.h>

#include "kmorph/config.h"

/* The config path kmorph-mkimage and the image builders it runs read. */
#define KMORPH_CONF_ENV "KMORPH_CONF"

/*
 * The image kmorph-mkimage keeps at KMORPH_INITRD_PATH, the distro's
 * initramfs with kmorphd or a standby root, rebuilt first when it is
 * missing or stale.
 */
int image_load(void **image, size_t *len);

/* kmorph image: builds the image afresh, unless initrd names one of the operator's own. */
int image_run(const struct kmorph_config *cfg);

/* One line for kmorph status: which image arm loads and whether it is current. */
void image_describe(const struct kmorph_config *cfg, FILE *out);

#endif

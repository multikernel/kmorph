#ifndef KMORPH_UPGRADE_H
#define KMORPH_UPGRADE_H

#include "kmorph/config.h"
#include "kmorph/mkfs.h"

/*
 * Hand the machine to the armed successor. Returns only on failure, with
 * this kernel still host.
 */
int upgrade_run(const struct kmorph_config *cfg, const char *name, const struct mkfs *fs);

#endif

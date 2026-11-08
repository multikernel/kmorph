#ifndef KMORPH_WATCH_H
#define KMORPH_WATCH_H

#include "kmorph/config.h"
#include "kmorph/mkfs.h"

/* Sends the network identity as it is now to kmorphd in instance @id. */
int watch_push(unsigned int id, const struct strlist *skip);

/*
 * Keeps the armed successor's copy of the network identity current. A
 * crash leaves no time to send it, so it goes whenever it changes.
 */
int watch_run(const struct kmorph_config *cfg, const char *name, const struct mkfs *fs);

#endif

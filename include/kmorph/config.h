#ifndef KMORPH_CONFIG_H
#define KMORPH_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "kmorph/parse.h"

#define KMORPH_CONFIG_PATH "/etc/kmorph/kmorph.conf"
#define KMORPH_INITRD_PATH "/var/lib/kmorph/successor.img"

struct kmorph_config {
	/* Successor to arm (predecessor side) */
	char *name;
	struct cpulist cpus;
	uint64_t memory;
	char *kernel;
	char *initrd;
	char *cmdline;
	struct strlist devices;	/* PCI ids handed to the successor */
	struct cpulist machine_cpus;	/* every CPU on the machine; empty: read the MADT */
	struct strlist modules;	/* kernel modules the image builders add; unused by kmorph itself */

	/* Detection (successor side) */
	uint64_t probe_interval_ms;
	unsigned int probe_timeouts;

	/* Takeover (successor side) */
	unsigned int fence_retries;
	char *dump;

	/* Console the successor takes over: a serial line it is armed with */
	char *console;			/* e.g. ttyS0 */
	unsigned int console_baud;
	char *console_login;		/* program run on the line; required with console */
	char *text;			/* the parsed text, as arm forwards it to the successor */
};

int config_parse(const char *text, struct kmorph_config *c, char *err, size_t errlen);
int config_load(const char *path, struct kmorph_config *c, char *err, size_t errlen);
void config_free(struct kmorph_config *c);

#endif

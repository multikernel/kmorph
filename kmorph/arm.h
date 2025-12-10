#ifndef KMORPH_ARM_H
#define KMORPH_ARM_H

#include "kmorph/config.h"
#include "kmorph/dt.h"
#include "kmorph/mkfs.h"

/* The syscalls and paths arming needs, injectable for tests. */
struct arm_hooks {
	int (*open_kernel)(const char *path);
	int (*kexec_load)(int kernel_fd, int initrd_fd, const char *cmdline, int mk_id);
	int (*exec)(int mk_id);
	int (*halt)(int mk_id);
	int (*unload)(int mk_id);
	const char *pci_root;		/* /sys/bus/pci/devices */
	const char *block_size_path;	/* memory block size, hex */
	const char *madt_path;		/* ACPI MADT, for the machine CPU list */
	const char *iomem_path;		/* /proc/iomem, for the machine RAM map */
};

extern const struct arm_hooks arm_default_hooks;

/* Pool devices the config names but the pool lacks; returns how many were added. */
int arm_pool_add_devices(const struct kmorph_config *cfg, const struct mkfs *fs,
			 const struct pool_view *pool);

int arm_run(const struct kmorph_config *cfg, const struct mkfs *fs, const struct arm_hooks *h);
int disarm_run(const struct kmorph_config *cfg, const struct mkfs *fs, const struct arm_hooks *h);

#endif

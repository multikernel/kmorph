#ifndef KMORPH_ARM_H
#define KMORPH_ARM_H

#include <stdbool.h>
#include <stdint.h>

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
	const char *vmcoreinfo_path;	/* /sys/kernel/vmcoreinfo */
	const char *cpu_root;		/* /sys/devices/system/cpu, for crash_notes */
	const char *kcore_path;		/* /proc/kcore, for the direct-map base */
	const char *default_initrd;	/* the image kmorph init wrote, when initrd is unset */
	const char *class_root;		/* /sys/class, to resolve device names */
	const char *cmdline_path;	/* /proc/cmdline, for the earlycon this kernel booted with */
	const char *usb_root;		/* /sys/bus/usb/devices, for usbN names */
	const char *own_host_tree;	/* the host tree this kernel booted with, if a spawn */
	const char *dt_cpus;		/* the firmware tree's /cpus, without ACPI */
	const char *pool_claim_path;	/* memory kmorphd kept for the pool, if this kernel took over */
	/* The network identity of the NICs but those in skip, as text; NULL: none is handed over. */
	int (*net_snapshot)(const struct strlist *skip, char **text);
};

extern const struct arm_hooks arm_default_hooks;

/* Pool devices the config names but the pool lacks; returns how many were added. */
int arm_pool_add_devices(const struct kmorph_config *cfg, const struct mkfs *fs,
			 const struct pool_view *pool);

/* An initramfs unpacks into the successor's RAM; three times its size is the floor. */
bool arm_memory_fits(uint64_t memory, size_t image_len);

/*
 * A standby image lists what it was built from in <image>.files; finds
 * the first of those changed since. Returns 1 with it in out, else 0.
 */
int arm_stale_source(const char *image, char *out, size_t len);

int arm_run(const struct kmorph_config *cfg, const char *name, const struct mkfs *fs,
	    const struct arm_hooks *h);
int disarm_run(const char *name, const struct mkfs *fs, const struct arm_hooks *h);

#endif

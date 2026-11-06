#ifndef KMORPH_DT_H
#define KMORPH_DT_H

#include <stddef.h>
#include <stdint.h>

#include "kmorph/host_tree.h"
#include "kmorph/takeable.h"
#include "kmorph/parse.h"
#include "kmorph/pci.h"
#include "kmorph/ranges.h"

/*
 * Blobs for the multikernel kernel interfaces. Every builder returns a
 * malloc'd DTB or DTBO the caller frees.
 */

/* Overlays for /sys/fs/multikernel/overlays/new */
/*
 * devices are PCI ids handed over by a second fragment; platform_devices
 * are platform device names listed in the instance's resources, which is
 * what lets the spawn register them. host_tree, when given, becomes the
 * multikernel,host-tree node of the instance's chosen node.
 */
int dt_build_instance_create(const char *name, const struct cpulist *cpus,
			     uint64_t memory_bytes, const struct strlist *devices,
			     const struct strlist *platform_devices,
			     const struct host_tree *host_tree,
			     void **dtbo, size_t *len);

/* A devices node of PCI functions, in the form the pool baseline uses. */
int dt_emit_pci_devices(void *fdt, const struct pci_list *devices);
int dt_build_instance_remove(const char *name, void **dtbo, size_t *len);
/* Adopted memory is the successor's for good: MK_MEM_NORMAL */
#define DT_MEM_NORMAL 0x01
int dt_build_adopt(const char *name, const struct cpulist *cpus,
		   const struct rangeset *memory, void **dtbo, size_t *len);
/* One function of the fenced predecessor, so that each succeeds or fails alone. */
int dt_build_adopt_device(const char *name, const char *pci_id, void **dtbo, size_t *len);
/* One IOMMU unit of the fenced predecessor, by its register base */
int dt_build_iommu_add(const char *name, uint64_t base, void **dtbo, size_t *len);

/* Baselines for /sys/fs/multikernel/device_tree */
/* The pool claims claim (may be NULL) and grows by what memory_bytes needs beyond it. */
int dt_build_pool_baseline(const struct cpulist *cpus, uint64_t memory_bytes,
			   const struct range *claim, const struct pci_list *devices,
			   const struct strlist *platform_devices, void **dtb, size_t *len);
int dt_build_pool_device_add(const struct strlist *devices, void **dtbo, size_t *len);
int dt_build_foreign_baseline(const struct takeable *t, void **dtb, size_t *len);

/* What the root tree read back from device_tree says about the pool */
struct pool_view {
	struct cpulist free_cpus;
	uint64_t total_bytes;
	uint64_t free_bytes;
	struct strlist devices;		/* PCI ids of the pool's devices */
	struct strlist platform_devices;	/* platform device names in the pool */
};

int dt_read_pool(const void *fdt, size_t len, struct pool_view *v);
void pool_view_free(struct pool_view *v);

/* The unit memory moves into or out of the kernel whose own tree this is in */
int dt_read_memory_granule(const void *fdt, size_t len, uint64_t *granule);

#endif

#ifndef KMORPH_CRASHINFO_H
#define KMORPH_CRASHINFO_H

#include <stdbool.h>
#include <stdint.h>

#include "kmorph/ranges.h"

#define VMCOREINFO_PATH "/sys/kernel/vmcoreinfo"
#define CPU_SYSFS_ROOT "/sys/devices/system/cpu"
#define KCORE_PATH "/proc/kcore"

/*
 * Where the predecessor kernel keeps what a vmcore needs beyond its
 * memory: the VMCOREINFO note, the per-CPU crash note buffers that
 * receive registers when a CPU stops, and its direct-map base. Read on
 * the predecessor at arm time, carried in the host tree, used by the
 * successor's writer.
 */
struct vmcore_info {
	bool has_page_offset;
	uint64_t page_offset;
	struct range vmcoreinfo;	/* size 0: absent */
	struct rangelist cpu_notes;	/* entry i is Linux CPU i; size 0: no buffer */
};

#define VMCORE_INFO_INIT { false, 0, { 0, 0 }, RANGELIST_INIT }

int crashinfo_read_vmcoreinfo(const char *path, struct range *out);
int crashinfo_read_cpu_notes(const char *cpu_root, struct rangelist *out);
int crashinfo_read_page_offset(const char *kcore_path, uint64_t *out);

bool vmcore_info_present(const struct vmcore_info *vi);
void vmcore_info_free(struct vmcore_info *vi);

#endif

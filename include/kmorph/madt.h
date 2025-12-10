#ifndef KMORPH_MADT_H
#define KMORPH_MADT_H

#include "kmorph/parse.h"

#define MADT_PATH "/sys/firmware/acpi/tables/APIC"

/*
 * Every CPU on the machine, by physical (APIC) id, from the firmware's
 * MADT: enabled or online-capable local APIC and x2APIC entries. This is
 * independent of which kernel currently owns each CPU.
 */
int madt_read_cpus(const char *path, struct cpulist *out);

#endif

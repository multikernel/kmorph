#ifndef KMORPH_MKSYS_H
#define KMORPH_MKSYS_H

#include <stdbool.h>

/* From uapi/linux/kexec.h of the multikernel tree. */
#define MK_KEXEC_FILE_UNLOAD		0x00000001
#define MK_KEXEC_FILE_NO_INITRAMFS	0x00000004
#define MK_KEXEC_FILE_MK_OPEN		0x00000040
#define MK_KEXEC_FILE_MK_ID_MASK	0x7fff0000
#define MK_KEXEC_FILE_MK_ID_SHIFT	16
#define MK_KEXEC_FILE_MULTIKERNEL	0x80000000

#define MK_ID_PREDECESSOR 0

#define MK_KEXEC_IOC_MAGIC		0xB9
#define MK_KEXEC_HALT_FORCE		0x00000001

/* An unload is refused while the instance runs, so a halt waits for it to stop. */
#define MK_HALT_TIMEOUT_MS		30000

unsigned long mksys_kexec_flags(int mk_id, bool has_initrd);
unsigned long mksys_kexec_unload_flags(int mk_id);
unsigned long mksys_kexec_open_flags(int mk_id);

/* The instance's descriptor, which the caller closes, or a negative errno. */
int mksys_kexec_load(int kernel_fd, int initrd_fd, const char *cmdline, int mk_id);

/* All return 0 or a negative errno. */
int mksys_kexec_unload(int mk_id);
int mksys_start(int fd);
int mksys_halt(int mk_id);
int mksys_fence(int mk_id);

#endif

#ifndef KMORPH_MKSYS_H
#define KMORPH_MKSYS_H

#include <stdbool.h>

/* From uapi/linux/reboot.h and uapi/linux/kexec.h of the multikernel tree. */
#define MK_REBOOT_CMD_EXEC		0x4D4B4C49
#define MK_REBOOT_CMD_HALT		0x4D4B4C48
#define MK_REBOOT_CMD_HALT_FORCE	0x4D4B4C46

#define MK_KEXEC_FILE_UNLOAD		0x00000001
#define MK_KEXEC_FILE_NO_INITRAMFS	0x00000004
#define MK_KEXEC_MULTIKERNEL		0x00000010
#define MK_KEXEC_MK_ID_MASK		0x0000ffe0
#define MK_KEXEC_MK_ID_SHIFT		5

#define MK_ID_PREDECESSOR 0

unsigned long mksys_kexec_flags(int mk_id, bool has_initrd);
unsigned long mksys_kexec_unload_flags(int mk_id);

/* All return 0 or a negative errno. */
int mksys_kexec_load(int kernel_fd, int initrd_fd, const char *cmdline, int mk_id);
int mksys_kexec_unload(int mk_id);
int mksys_exec(int mk_id);
int mksys_halt(int mk_id);
int mksys_fence(int mk_id);

#endif

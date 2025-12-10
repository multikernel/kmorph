#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "kmorph/mksys.h"

#define MK_REBOOT_MAGIC1 0xfee1dead
#define MK_REBOOT_MAGIC2 672274793

struct mk_boot_args {
	int mk_id;
};

unsigned long mksys_kexec_flags(int mk_id, bool has_initrd)
{
	unsigned long flags = MK_KEXEC_MULTIKERNEL;

	flags |= ((unsigned long)mk_id << MK_KEXEC_MK_ID_SHIFT) & MK_KEXEC_MK_ID_MASK;
	if (!has_initrd)
		flags |= MK_KEXEC_FILE_NO_INITRAMFS;
	return flags;
}

unsigned long mksys_kexec_unload_flags(int mk_id)
{
	return mksys_kexec_flags(mk_id, true) | MK_KEXEC_FILE_UNLOAD;
}

static int kexec_file_load(int kernel_fd, int initrd_fd, const char *cmdline,
			   unsigned long flags)
{
#ifdef SYS_kexec_file_load
	unsigned long len = cmdline ? strlen(cmdline) + 1 : 0;
	long ret;

	ret = syscall(SYS_kexec_file_load, kernel_fd, initrd_fd, len, cmdline, flags);
	return ret < 0 ? -errno : 0;
#else
	(void)kernel_fd; (void)initrd_fd; (void)cmdline; (void)flags;
	return -ENOSYS;
#endif
}

int mksys_kexec_load(int kernel_fd, int initrd_fd, const char *cmdline, int mk_id)
{
	return kexec_file_load(kernel_fd, initrd_fd, cmdline,
			       mksys_kexec_flags(mk_id, initrd_fd >= 0));
}

int mksys_kexec_unload(int mk_id)
{
	return kexec_file_load(-1, -1, NULL, mksys_kexec_unload_flags(mk_id));
}

static int mksys_reboot(unsigned int cmd, int mk_id)
{
	struct mk_boot_args args = { .mk_id = mk_id };
	long ret;

	ret = syscall(SYS_reboot, MK_REBOOT_MAGIC1, MK_REBOOT_MAGIC2, cmd, &args);
	return ret < 0 ? -errno : 0;
}

int mksys_exec(int mk_id)
{
	return mksys_reboot(MK_REBOOT_CMD_EXEC, mk_id);
}

int mksys_halt(int mk_id)
{
	return mksys_reboot(MK_REBOOT_CMD_HALT, mk_id);
}

int mksys_fence(int mk_id)
{
	return mksys_reboot(MK_REBOOT_CMD_HALT_FORCE, mk_id);
}

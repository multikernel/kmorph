#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "kmorph/mksys.h"

/* uapi/linux/kexec.h of the multikernel tree. */
struct mk_start {
	uint32_t flags;
	uint32_t timeout_ms;
	uint64_t cmdline;
	uint32_t cmdline_len;
	uint32_t pad;
};

struct mk_halt {
	uint32_t flags;
	uint32_t timeout_ms;
};

#define MK_IOC_START	_IOW(MK_KEXEC_IOC_MAGIC, 1, struct mk_start)
#define MK_IOC_HALT	_IOW(MK_KEXEC_IOC_MAGIC, 2, struct mk_halt)

unsigned long mksys_kexec_flags(int mk_id, bool has_initrd)
{
	unsigned long flags = MK_KEXEC_FILE_MULTIKERNEL;

	flags |= ((unsigned long)mk_id << MK_KEXEC_FILE_MK_ID_SHIFT) & MK_KEXEC_FILE_MK_ID_MASK;
	if (!has_initrd)
		flags |= MK_KEXEC_FILE_NO_INITRAMFS;
	return flags;
}

unsigned long mksys_kexec_unload_flags(int mk_id)
{
	return mksys_kexec_flags(mk_id, true) | MK_KEXEC_FILE_UNLOAD;
}

unsigned long mksys_kexec_open_flags(int mk_id)
{
	return mksys_kexec_flags(mk_id, true) | MK_KEXEC_FILE_MK_OPEN;
}

static int kexec_file_load(int kernel_fd, int initrd_fd, const char *cmdline,
			   unsigned long flags)
{
#ifdef SYS_kexec_file_load
	unsigned long len = cmdline ? strlen(cmdline) + 1 : 0;
	long ret;

	ret = syscall(SYS_kexec_file_load, kernel_fd, initrd_fd, len, cmdline, flags);
	return ret < 0 ? -errno : (int)ret;
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
	int ret = kexec_file_load(-1, -1, NULL, mksys_kexec_unload_flags(mk_id));

	return ret < 0 ? ret : 0;
}

int mksys_start(int fd)
{
	struct mk_start start = { 0 };

	return ioctl(fd, MK_IOC_START, &start) < 0 ? -errno : 0;
}

static int halt_instance(int mk_id, uint32_t flags, uint32_t timeout_ms)
{
	struct mk_halt halt = { .flags = flags, .timeout_ms = timeout_ms };
	int fd, ret;

	fd = kexec_file_load(-1, -1, NULL, mksys_kexec_open_flags(mk_id));
	if (fd < 0)
		return fd;
	ret = ioctl(fd, MK_IOC_HALT, &halt) < 0 ? -errno : 0;
	close(fd);
	return ret;
}

int mksys_halt(int mk_id)
{
	return halt_instance(mk_id, 0, MK_HALT_TIMEOUT_MS);
}

int mksys_fence(int mk_id)
{
	return halt_instance(mk_id, MK_KEXEC_HALT_FORCE, 0);
}

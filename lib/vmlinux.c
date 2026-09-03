#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/vmlinux.h"

/* x86 boot protocol header fields (Documentation/arch/x86/boot.rst) */
#define BZ_SETUP_SECTS		0x1f1
#define BZ_HEADER_MAGIC		0x202
#define BZ_VERSION		0x206
#define BZ_PAYLOAD_OFFSET	0x248
#define BZ_PAYLOAD_LENGTH	0x24c
#define BZ_HEADER_END		0x250
#define BZ_MIN_VERSION		0x0208

static uint16_t le16(const unsigned char *p)
{
	return p[0] | (p[1] << 8);
}

static uint32_t le32(const unsigned char *p)
{
	return le16(p) | ((uint32_t)le16(p + 2) << 16);
}

bool vmlinux_is_bzimage(const void *image, size_t len)
{
	const unsigned char *p = image;

	return len >= BZ_HEADER_END && !memcmp(p + BZ_HEADER_MAGIC, "HdrS", 4);
}

int vmlinux_locate_payload(const void *image, size_t len, size_t *offset, size_t *payload_len)
{
	const unsigned char *p = image;
	size_t setup_sects, start;

	if (!vmlinux_is_bzimage(image, len) || le16(p + BZ_VERSION) < BZ_MIN_VERSION)
		return -EINVAL;

	setup_sects = p[BZ_SETUP_SECTS] ? p[BZ_SETUP_SECTS] : 4;
	start = (setup_sects + 1) * 512 + le32(p + BZ_PAYLOAD_OFFSET);
	*payload_len = le32(p + BZ_PAYLOAD_LENGTH);
	if (start > len || *payload_len > len - start)
		return -EINVAL;
	*offset = start;
	return 0;
}

static const struct {
	const char *magic;
	size_t magic_len;
	const char *name;
} decompressors[] = {
	{ "\x1f\x8b", 2, "gzip" },
	{ "\xfd" "7zXZ\x00", 6, "xz" },
	{ "\x28\xb5\x2f\xfd", 4, "zstd" },
	{ "\x02\x21\x4c\x18", 4, "lz4" },
	{ "\x04\x22\x4d\x18", 4, "lz4" },
	{ "\x5d\x00", 2, "lzma" },
};

const char *vmlinux_decompressor(const void *payload, size_t len)
{
	size_t i;

	for (i = 0; i < sizeof(decompressors) / sizeof(*decompressors); i++)
		if (len >= decompressors[i].magic_len &&
		    !memcmp(payload, decompressors[i].magic, decompressors[i].magic_len))
			return decompressors[i].name;
	return NULL;
}

/*
 * A relocatable kernel appends its relocation table after the ELF image;
 * the loader wants the image alone.
 */
size_t vmlinux_elf_size(const void *elf, size_t len)
{
	const Elf64_Ehdr *eh = elf;
	size_t end, i;

	if (len < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
	    eh->e_ident[EI_CLASS] != ELFCLASS64)
		return 0;

	end = sizeof(*eh);
	if (eh->e_phoff + (size_t)eh->e_phentsize * eh->e_phnum > end)
		end = eh->e_phoff + (size_t)eh->e_phentsize * eh->e_phnum;
	if (eh->e_shoff + (size_t)eh->e_shentsize * eh->e_shnum > end)
		end = eh->e_shoff + (size_t)eh->e_shentsize * eh->e_shnum;
	for (i = 0; i < eh->e_phnum; i++) {
		const Elf64_Phdr *ph;

		if (eh->e_phoff + (i + 1) * sizeof(*ph) > len)
			return 0;
		ph = (const Elf64_Phdr *)((const char *)elf + eh->e_phoff + i * eh->e_phentsize);
		if (ph->p_offset + ph->p_filesz > end)
			end = ph->p_offset + ph->p_filesz;
	}
	return end;
}

static int memfd_with(const char *name, const void *data, size_t len)
{
	int fd = memfd_create(name, MFD_CLOEXEC);

	if (fd < 0)
		return -errno;
	while (len) {
		ssize_t n = write(fd, data, len);

		if (n < 0) {
			close(fd);
			return -errno;
		}
		data = (const char *)data + n;
		len -= n;
	}
	lseek(fd, 0, SEEK_SET);
	return fd;
}

static int run_decompressor(const char *tool, int in_fd, int out_fd)
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		return -errno;
	if (pid == 0) {
		dup2(in_fd, STDIN_FILENO);
		dup2(out_fd, STDOUT_FILENO);
		if (!strcmp(tool, "lzma"))
			execlp("xz", "xz", "--format=lzma", "-dc", (char *)NULL);
		else
			execlp(tool, tool, "-dc", (char *)NULL);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
		return -ENOEXEC;
	/* Some tools exit nonzero over trailing bytes; the ELF check decides. */
	return 0;
}

static int trim_to_elf(int fd)
{
	unsigned char head[64 * 1024];
	off_t total = lseek(fd, 0, SEEK_END);
	ssize_t n = pread(fd, head, sizeof(head), 0);
	size_t elf_len;

	if (total <= 0 || n <= 0)
		return -EINVAL;
	elf_len = vmlinux_elf_size(head, n);
	if (!elf_len || (off_t)elf_len > total)
		return -EINVAL;
	if ((off_t)elf_len < total && ftruncate(fd, elf_len) < 0)
		return -errno;
	lseek(fd, 0, SEEK_SET);
	return 0;
}

static int extract_vmlinux(const unsigned char *image, size_t len)
{
	const char *tool;
	size_t off, plen;
	int in_fd, out_fd, ret;

	ret = vmlinux_locate_payload(image, len, &off, &plen);
	if (ret)
		return ret;
	tool = vmlinux_decompressor(image + off, plen);
	if (!tool)
		return -ENOTSUP;

	in_fd = memfd_with("kmorph-payload", image + off, plen);
	if (in_fd < 0)
		return in_fd;
	out_fd = memfd_create("kmorph-vmlinux", MFD_CLOEXEC);
	if (out_fd < 0) {
		close(in_fd);
		return -errno;
	}

	ret = run_decompressor(tool, in_fd, out_fd);
	close(in_fd);
	if (!ret)
		ret = trim_to_elf(out_fd);
	if (ret) {
		close(out_fd);
		return ret;
	}
	return out_fd;
}

int vmlinux_open(const char *path)
{
	void *image;
	size_t len;
	int ret, fd;

	ret = file_read(path, &image, &len);
	if (ret)
		return ret;

	if (vmlinux_is_bzimage(image, len))
		fd = extract_vmlinux(image, len);
	else if (vmlinux_elf_size(image, len))
		fd = open(path, O_RDONLY | O_CLOEXEC);
	else
		fd = -EINVAL;
	if (fd == -1)
		fd = -errno;

	free(image);
	return fd;
}

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/crashinfo.h"
#include "kmorph/file.h"

/* Start of the kernel text window on x86-64; the direct map lies below it. */
#define KERNEL_TEXT_BASE 0xffffffff80000000ULL

static int parse_hex(const char *s, char **end, uint64_t *v)
{
	if (!*s)
		return -EINVAL;
	*v = strtoull(s, end, 16);
	return *end == s ? -EINVAL : 0;
}

int crashinfo_read_vmcoreinfo(const char *path, struct range *out)
{
	char *text, *p;
	uint64_t base, size;
	int ret;

	ret = file_read_string(path, &text);
	if (ret)
		return ret;
	ret = parse_hex(text, &p, &base);
	if (!ret && *p == ' ')
		ret = parse_hex(p + 1, &p, &size);
	else
		ret = -EINVAL;
	if (!ret && (*p || !size))
		ret = -EINVAL;
	free(text);
	if (ret)
		return ret;
	out->base = base;
	out->size = size;
	return 0;
}

static int read_hex_file(const char *path, uint64_t *v)
{
	char *text, *end;
	int ret;

	ret = file_read_string(path, &text);
	if (ret)
		return ret;
	ret = parse_hex(text, &end, v);
	if (!ret && *end)
		ret = -EINVAL;
	free(text);
	return ret;
}

static bool cpu_dir_exists(const char *cpu_root, size_t n)
{
	char path[PATH_MAX];
	struct stat st;

	snprintf(path, sizeof(path), "%s/cpu%zu", cpu_root, n);
	return !stat(path, &st) && S_ISDIR(st.st_mode);
}

static void read_cpu_note(const char *cpu_root, size_t n, struct range *r)
{
	char path[PATH_MAX];
	uint32_t size;

	r->base = 0;
	r->size = 0;
	snprintf(path, sizeof(path), "%s/cpu%zu/crash_notes", cpu_root, n);
	if (read_hex_file(path, &r->base))
		return;
	snprintf(path, sizeof(path), "%s/cpu%zu/crash_notes_size", cpu_root, n);
	if (file_read_u32(path, &size)) {
		r->base = 0;
		return;
	}
	r->size = size;
}

int crashinfo_read_cpu_notes(const char *cpu_root, struct rangelist *out)
{
	size_t n, found = 0;

	out->r = NULL;
	out->count = 0;
	for (n = 0; cpu_dir_exists(cpu_root, n); n++) {
		struct range r;
		int ret;

		read_cpu_note(cpu_root, n, &r);
		ret = rangelist_add(out, r.base, r.size);
		if (ret) {
			rangelist_free(out);
			return ret;
		}
		if (r.size)
			found++;
	}
	if (!found) {
		rangelist_free(out);
		return -ENOENT;
	}
	return 0;
}

int crashinfo_read_page_offset(const char *kcore_path, uint64_t *out)
{
	Elf64_Ehdr eh;
	Elf64_Phdr *ph;
	size_t i, len;
	int fd, ret = -ENOENT;

	fd = open(kcore_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	if (pread(fd, &eh, sizeof(eh), 0) != (ssize_t)sizeof(eh) ||
	    memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
	    eh.e_phentsize != sizeof(*ph)) {
		close(fd);
		return -EINVAL;
	}
	len = (size_t)eh.e_phnum * sizeof(*ph);
	/* +1 so an e_phnum of 0 does not make malloc's NULL look like -ENOMEM. */
	ph = malloc(len + 1);
	if (!ph) {
		close(fd);
		return -ENOMEM;
	}
	if (pread(fd, ph, len, eh.e_phoff) != (ssize_t)len) {
		ret = -EIO;
		goto out;
	}
	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD || ph[i].p_paddr == (Elf64_Addr)-1 ||
		    ph[i].p_vaddr >= KERNEL_TEXT_BASE)
			continue;
		*out = ph[i].p_vaddr - ph[i].p_paddr;
		ret = 0;
		break;
	}
out:
	free(ph);
	close(fd);
	return ret;
}

bool vmcore_info_present(const struct vmcore_info *vi)
{
	return vi->has_page_offset || vi->vmcoreinfo.size || vi->cpu_notes.count;
}

void vmcore_info_free(struct vmcore_info *vi)
{
	rangelist_free(&vi->cpu_notes);
	vi->vmcoreinfo.base = 0;
	vi->vmcoreinfo.size = 0;
	vi->has_page_offset = false;
	vi->page_offset = 0;
}

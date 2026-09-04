#include <elf.h>
#include <errno.h>
#include <string.h>

#include "kmorph/elf.h"

int elf_needs_interp(const void *buf, size_t len)
{
	const Elf64_Ehdr *eh = buf;
	size_t i;

	if (len < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) ||
	    eh->e_ident[EI_CLASS] != ELFCLASS64 ||
	    eh->e_phentsize != sizeof(Elf64_Phdr) ||
	    eh->e_phoff + (size_t)eh->e_phnum * sizeof(Elf64_Phdr) > len)
		return -ENOEXEC;
	for (i = 0; i < eh->e_phnum; i++) {
		const Elf64_Phdr *ph = (const Elf64_Phdr *)
			((const char *)buf + eh->e_phoff + i * sizeof(*ph));

		if (ph->p_type == PT_INTERP)
			return 1;
	}
	return 0;
}

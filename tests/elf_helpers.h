#ifndef KMORPH_TEST_ELF_HELPERS_H
#define KMORPH_TEST_ELF_HELPERS_H

#include <elf.h>
#include <string.h>

/* A minimal x86-64 executable: one PT_LOAD, plus a PT_INTERP when asked. */
static size_t fake_elf64(unsigned char *buf, int interp)
{
	Elf64_Ehdr *eh = (Elf64_Ehdr *)buf;
	Elf64_Phdr *ph = (Elf64_Phdr *)(buf + sizeof(*eh));
	int n = interp ? 2 : 1;

	memset(buf, 0, sizeof(*eh) + n * sizeof(*ph) + 16);
	memcpy(eh->e_ident, ELFMAG, SELFMAG);
	eh->e_ident[EI_CLASS] = ELFCLASS64;
	eh->e_ident[EI_DATA] = ELFDATA2LSB;
	eh->e_type = ET_EXEC;
	eh->e_machine = EM_X86_64;
	eh->e_phoff = sizeof(*eh);
	eh->e_phentsize = sizeof(*ph);
	eh->e_phnum = n;
	ph[0].p_type = PT_LOAD;
	ph[0].p_offset = sizeof(*eh) + n * sizeof(*ph);
	ph[0].p_filesz = 16;
	if (interp) {
		ph[1].p_type = PT_INTERP;
		ph[1].p_offset = ph[0].p_offset;
		ph[1].p_filesz = 16;
	}
	return sizeof(*eh) + n * sizeof(*ph) + 16;
}

#endif

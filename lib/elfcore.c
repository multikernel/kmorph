#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/elfcore.h"

static uint64_t page_align(uint64_t v)
{
	return (v + ELFCORE_PAGE - 1) & ~(ELFCORE_PAGE - 1);
}

static size_t note_align(size_t v)
{
	return (v + 3) & ~(size_t)3;
}

size_t elfcore_notes_len(const void *buf, size_t len)
{
	const unsigned char *p = buf;
	size_t off = 0;

	while (off + sizeof(Elf64_Nhdr) <= len) {
		Elf64_Nhdr nh;
		size_t rec;

		memcpy(&nh, p + off, sizeof(nh));
		if (!nh.n_namesz && !nh.n_descsz)
			break;
		rec = sizeof(nh) + note_align(nh.n_namesz) + note_align(nh.n_descsz);
		if (off + rec > len)
			break;
		off += rec;
	}
	return off;
}

static void fill_ehdr(Elf64_Ehdr *eh, size_t phnum)
{
	memset(eh, 0, sizeof(*eh));
	memcpy(eh->e_ident, ELFMAG, SELFMAG);
	eh->e_ident[EI_CLASS] = ELFCLASS64;
	eh->e_ident[EI_DATA] = ELFDATA2LSB;
	eh->e_ident[EI_VERSION] = EV_CURRENT;
	eh->e_ident[EI_OSABI] = ELFOSABI_NONE;
	eh->e_type = ET_CORE;
	eh->e_machine = EM_X86_64;
	eh->e_version = EV_CURRENT;
	eh->e_phoff = sizeof(*eh);
	eh->e_ehsize = sizeof(*eh);
	eh->e_phentsize = sizeof(Elf64_Phdr);
	eh->e_phnum = phnum;
}

int elfcore_layout(const struct rangeset *ranges, const struct elfcore_note *notes,
		   size_t nnotes, bool has_page_offset, uint64_t page_offset,
		   struct elfcore_layout *out)
{
	size_t phnum = 1 + ranges->count, notes_len = 0, i;
	uint64_t note_off = sizeof(Elf64_Ehdr) + phnum * sizeof(Elf64_Phdr), cur;
	Elf64_Phdr *ph;
	unsigned char *p;

	/* e_phnum is a uint16_t; a range count that overflows it cannot be laid out. */
	if (ranges->count > UINT16_MAX - 1)
		return -E2BIG;

	for (i = 0; i < nnotes; i++)
		notes_len += notes[i].len;

	memset(out, 0, sizeof(*out));
	out->header_len = page_align(note_off + notes_len);
	out->header = calloc(1, out->header_len);
	/* +1 so a count of 0 does not make calloc's NULL look like -ENOMEM. */
	out->offsets = calloc(ranges->count + 1, sizeof(*out->offsets));
	if (!out->header || !out->offsets) {
		elfcore_layout_free(out);
		return -ENOMEM;
	}

	fill_ehdr(out->header, phnum);
	ph = (Elf64_Phdr *)((unsigned char *)out->header + sizeof(Elf64_Ehdr));
	ph[0].p_type = PT_NOTE;
	ph[0].p_offset = note_off;
	ph[0].p_filesz = notes_len;
	ph[0].p_memsz = notes_len;
	ph[0].p_align = 4;
	p = (unsigned char *)out->header + note_off;
	for (i = 0; i < nnotes; i++) {
		memcpy(p, notes[i].data, notes[i].len);
		p += notes[i].len;
	}

	cur = out->header_len;
	for (i = 0; i < ranges->count; i++) {
		const struct range *r = &ranges->r[i];
		Elf64_Phdr *load = &ph[1 + i];

		load->p_type = PT_LOAD;
		load->p_flags = PF_R | PF_W | PF_X;
		load->p_offset = cur;
		load->p_paddr = r->base;
		load->p_vaddr = has_page_offset ? page_offset + r->base : ELFCORE_NO_VADDR;
		load->p_filesz = r->size;
		load->p_memsz = r->size;
		load->p_align = ELFCORE_PAGE;
		out->offsets[i] = cur;
		cur = page_align(cur + r->size);
	}
	out->total = cur;
	return 0;
}

void elfcore_layout_free(struct elfcore_layout *l)
{
	free(l->header);
	free(l->offsets);
	memset(l, 0, sizeof(*l));
}

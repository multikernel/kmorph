#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "test.h"
#include "kmorph/elfcore.h"

/* One "CORE" note with an 8-byte desc, then the terminator, as the kernel writes them. */
static size_t make_note(unsigned char *buf, uint32_t type, const char *name, size_t desc_len)
{
	Elf64_Nhdr nh = { strlen(name) + 1, desc_len, type };
	size_t namesz = (nh.n_namesz + 3) & ~3u, descsz = (desc_len + 3) & ~3u;
	size_t off = 0;

	memcpy(buf, &nh, sizeof(nh));
	off += sizeof(nh);
	memset(buf + off, 0, namesz + descsz);
	memcpy(buf + off, name, nh.n_namesz);
	off += namesz;
	memset(buf + off, 0xab, desc_len);
	off += descsz;
	memset(buf + off, 0, sizeof(nh));
	return off;
}

static void notes_len_stops_at_the_terminator(void)
{
	unsigned char buf[256];
	size_t n;

	memset(buf, 0, sizeof(buf));
	n = make_note(buf, NT_PRSTATUS, "CORE", 8);
	CHECK_EQ(n, 12 + 8 + 8);
	CHECK_EQ(elfcore_notes_len(buf, sizeof(buf)), n);
	CHECK_EQ(elfcore_notes_len(buf, n), n);		/* record ends exactly at the buffer end */
	CHECK_EQ(elfcore_notes_len(buf, 20), 0);		/* record does not fit */
	memset(buf, 0, sizeof(buf));
	CHECK_EQ(elfcore_notes_len(buf, sizeof(buf)), 0);
	CHECK_EQ(elfcore_notes_len(buf, 4), 0);
}

static void layout_places_notes_then_page_aligned_loads(void)
{
	unsigned char n1[64], n2[64];
	struct elfcore_note notes[2];
	struct rangeset ranges = RANGESET_INIT;
	struct elfcore_layout l;
	Elf64_Ehdr *eh;
	Elf64_Phdr *ph;

	notes[0].len = make_note(n1, NT_PRSTATUS, "CORE", 8);
	notes[0].data = n1;
	notes[1].len = make_note(n2, 0, "VMCOREINFO", 16);
	notes[1].data = n2;
	rangeset_add(&ranges, 0x1000, 0x7000);
	rangeset_add(&ranges, 0x100000, 0x100800);

	CHECK_EQ(elfcore_layout(&ranges, notes, 2, true, 0xffff888000000000ULL, &l), 0);
	eh = l.header;
	CHECK_EQ(memcmp(eh->e_ident, ELFMAG, SELFMAG), 0);
	CHECK_EQ(eh->e_ident[EI_CLASS], ELFCLASS64);
	CHECK_EQ(eh->e_type, ET_CORE);
	CHECK_EQ(eh->e_machine, EM_X86_64);
	CHECK_EQ(eh->e_phoff, sizeof(*eh));
	CHECK_EQ(eh->e_phnum, 3);
	ph = (Elf64_Phdr *)((char *)l.header + eh->e_phoff);
	CHECK_EQ(ph[0].p_type, PT_NOTE);
	CHECK_EQ(ph[0].p_offset, sizeof(*eh) + 3 * sizeof(*ph));
	CHECK_EQ(ph[0].p_filesz, notes[0].len + notes[1].len);
	CHECK_EQ(memcmp((char *)l.header + ph[0].p_offset, n1, notes[0].len), 0);
	CHECK_EQ(memcmp((char *)l.header + ph[0].p_offset + notes[0].len, n2, notes[1].len), 0);
	CHECK_EQ(l.header_len, ELFCORE_PAGE);
	CHECK_EQ(ph[1].p_type, PT_LOAD);
	CHECK_EQ(ph[1].p_offset, ELFCORE_PAGE);
	CHECK_EQ(ph[1].p_paddr, 0x1000);
	CHECK_EQ(ph[1].p_vaddr, 0xffff888000001000ULL);
	CHECK_EQ(ph[1].p_filesz, 0x7000);
	CHECK_EQ(ph[1].p_memsz, 0x7000);
	CHECK_EQ(ph[1].p_align, ELFCORE_PAGE);
	CHECK_EQ(ph[1].p_flags, PF_R | PF_W | PF_X);
	CHECK_EQ(l.offsets[0], ELFCORE_PAGE);
	CHECK_EQ(ph[2].p_offset, ELFCORE_PAGE + 0x7000);
	CHECK_EQ(l.offsets[1], ELFCORE_PAGE + 0x7000);
	CHECK_EQ(l.total, ELFCORE_PAGE + 0x7000 + 0x101000);	/* odd size rounded up */
	elfcore_layout_free(&l);
	rangeset_free(&ranges);
}

static void layout_with_no_ranges_has_only_the_note_header(void)
{
	unsigned char n1[64];
	struct elfcore_note note;
	struct rangeset ranges = RANGESET_INIT;
	struct elfcore_layout l;
	Elf64_Ehdr *eh;
	Elf64_Phdr *ph;

	note.len = make_note(n1, NT_PRSTATUS, "CORE", 8);
	note.data = n1;

	CHECK_EQ(elfcore_layout(&ranges, &note, 1, false, 0, &l), 0);
	eh = l.header;
	CHECK_EQ(eh->e_phnum, 1);
	ph = (Elf64_Phdr *)((char *)l.header + eh->e_phoff);
	CHECK_EQ(ph[0].p_type, PT_NOTE);
	CHECK_EQ(l.header_len, ELFCORE_PAGE);
	CHECK_EQ(l.total, ELFCORE_PAGE);
	elfcore_layout_free(&l);
	rangeset_free(&ranges);
}

static void layout_without_page_offset_marks_no_vaddr(void)
{
	struct rangeset ranges = RANGESET_INIT;
	struct elfcore_layout l;
	Elf64_Phdr *ph;

	rangeset_add(&ranges, 0x100000, 0x1000);
	CHECK_EQ(elfcore_layout(&ranges, NULL, 0, false, 0, &l), 0);
	ph = (Elf64_Phdr *)((char *)l.header + sizeof(Elf64_Ehdr));
	CHECK_EQ(ph[0].p_type, PT_NOTE);
	CHECK_EQ(ph[0].p_filesz, 0);
	CHECK_EQ(ph[1].p_vaddr, ELFCORE_NO_VADDR);
	CHECK_EQ(l.total, ELFCORE_PAGE + 0x1000);
	elfcore_layout_free(&l);
	rangeset_free(&ranges);
}

static void large_notes_push_the_loads_to_the_next_page(void)
{
	static unsigned char big[ELFCORE_PAGE];
	struct elfcore_note note = { big, sizeof(big) };
	struct rangeset ranges = RANGESET_INIT;
	struct elfcore_layout l;

	rangeset_add(&ranges, 0, 0x1000);
	CHECK_EQ(elfcore_layout(&ranges, &note, 1, false, 0, &l), 0);
	CHECK_EQ(l.header_len, 2 * ELFCORE_PAGE);
	CHECK_EQ(l.offsets[0], 2 * ELFCORE_PAGE);
	elfcore_layout_free(&l);
	rangeset_free(&ranges);
}

/* count alone overflows e_phnum; no r[] entries are touched before the check. */
static void a_range_count_that_overflows_e_phnum_is_rejected(void)
{
	struct rangeset ranges = { NULL, UINT16_MAX, 0 };
	struct elfcore_layout l;

	CHECK_EQ(elfcore_layout(&ranges, NULL, 0, false, 0, &l), -E2BIG);
}

TEST_MAIN({
	RUN(notes_len_stops_at_the_terminator);
	RUN(layout_places_notes_then_page_aligned_loads);
	RUN(layout_with_no_ranges_has_only_the_note_header);
	RUN(layout_without_page_offset_marks_no_vaddr);
	RUN(large_notes_push_the_loads_to_the_next_page);
	RUN(a_range_count_that_overflows_e_phnum_is_rejected);
})

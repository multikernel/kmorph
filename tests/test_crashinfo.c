#include <elf.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/crashinfo.h"
#include "kmorph/file.h"

static char dir[] = "/tmp/kmorph-test-crashinfo-XXXXXX";

static void put(const char *rel, const char *text)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	CHECK_EQ(file_write(path, text, strlen(text)), 0);
}

static void mkdir_rel(const char *rel)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	mkdir(path, 0755);
}

static void reads_vmcoreinfo_address_and_size(void)
{
	char path[512];
	struct range r;

	snprintf(path, sizeof(path), "%s/vmcoreinfo", dir);
	put("vmcoreinfo", "0x000000007ffd1000 1000\n");
	CHECK_EQ(crashinfo_read_vmcoreinfo(path, &r), 0);
	CHECK_EQ(r.base, 0x7ffd1000);
	CHECK_EQ(r.size, 0x1000);

	put("vmcoreinfo", "garbage\n");
	CHECK_EQ(crashinfo_read_vmcoreinfo(path, &r), -EINVAL);
	CHECK_EQ(crashinfo_read_vmcoreinfo("/nonexistent", &r), -ENOENT);
}

static void reads_crash_notes_in_cpu_order(void)
{
	char root[512];
	struct rangelist l;

	snprintf(root, sizeof(root), "%s/cpu", dir);
	mkdir_rel("cpu");
	mkdir_rel("cpu/cpu0");
	put("cpu/cpu0/crash_notes", "7fc01000\n");
	put("cpu/cpu0/crash_notes_size", "1024\n");
	mkdir_rel("cpu/cpu1");
	put("cpu/cpu1/crash_notes", "7fc01400\n");
	put("cpu/cpu1/crash_notes_size", "1024\n");
	mkdir_rel("cpu/cpu2");
	mkdir_rel("cpu/cpu3");
	put("cpu/cpu3/crash_notes", "7fc01c00\n");
	put("cpu/cpu3/crash_notes_size", "1024\n");
	mkdir_rel("cpu/cpufreq");

	CHECK_EQ(crashinfo_read_cpu_notes(root, &l), 0);
	CHECK_EQ(l.count, 4);
	CHECK_EQ(l.r[0].base, 0x7fc01000);
	CHECK_EQ(l.r[1].base, 0x7fc01400);
	CHECK_EQ(l.r[1].size, 1024);
	CHECK_EQ(l.r[2].size, 0);
	CHECK_EQ(l.r[3].base, 0x7fc01c00);
	rangelist_free(&l);
}

static void cpus_without_notes_are_enoent(void)
{
	char root[512];
	struct rangelist l;

	snprintf(root, sizeof(root), "%s/nocrash", dir);
	mkdir_rel("nocrash");
	mkdir_rel("nocrash/cpu0");
	mkdir_rel("nocrash/cpu1");
	CHECK_EQ(crashinfo_read_cpu_notes(root, &l), -ENOENT);
	CHECK_EQ(l.count, 0);
	CHECK_EQ(crashinfo_read_cpu_notes("/nonexistent", &l), -ENOENT);
}

static void put_kcore(const char *rel, const Elf64_Phdr *ph, size_t n)
{
	char path[512];
	Elf64_Ehdr eh;
	unsigned char *blob;
	size_t len = sizeof(eh) + n * sizeof(*ph);

	memset(&eh, 0, sizeof(eh));
	memcpy(eh.e_ident, ELFMAG, SELFMAG);
	eh.e_ident[EI_CLASS] = ELFCLASS64;
	eh.e_ident[EI_DATA] = ELFDATA2LSB;
	eh.e_type = ET_CORE;
	eh.e_machine = EM_X86_64;
	eh.e_phoff = sizeof(eh);
	eh.e_ehsize = sizeof(eh);
	eh.e_phentsize = sizeof(*ph);
	eh.e_phnum = n;
	blob = malloc(len);
	memcpy(blob, &eh, sizeof(eh));
	memcpy(blob + sizeof(eh), ph, n * sizeof(*ph));
	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	CHECK_EQ(file_write(path, blob, len), 0);
	free(blob);
}

static void page_offset_comes_from_the_direct_map_segment(void)
{
	char path[512];
	Elf64_Phdr ph[4];
	uint64_t off = 0;

	memset(ph, 0, sizeof(ph));
	ph[0].p_type = PT_NOTE;
	ph[1].p_type = PT_LOAD;			/* kernel text: above the text window */
	ph[1].p_vaddr = 0xffffffff81000000ULL;
	ph[1].p_paddr = 0x1000000;
	ph[2].p_type = PT_LOAD;			/* vmalloc: no physical address */
	ph[2].p_vaddr = 0xffffc90000000000ULL;
	ph[2].p_paddr = (Elf64_Addr)-1;
	ph[3].p_type = PT_LOAD;			/* direct map */
	ph[3].p_vaddr = 0xffff888000100000ULL;
	ph[3].p_paddr = 0x100000;
	put_kcore("kcore", ph, 4);
	snprintf(path, sizeof(path), "%s/kcore", dir);
	CHECK_EQ(crashinfo_read_page_offset(path, &off), 0);
	CHECK_EQ(off, 0xffff888000000000ULL);

	put_kcore("kcore-nodm", ph, 3);
	snprintf(path, sizeof(path), "%s/kcore-nodm", dir);
	CHECK_EQ(crashinfo_read_page_offset(path, &off), -ENOENT);

	put_kcore("kcore-empty", ph, 0);
	snprintf(path, sizeof(path), "%s/kcore-empty", dir);
	CHECK_EQ(crashinfo_read_page_offset(path, &off), -ENOENT);

	put("notelf", "hello");
	snprintf(path, sizeof(path), "%s/notelf", dir);
	CHECK_EQ(crashinfo_read_page_offset(path, &off), -EINVAL);
	CHECK_EQ(crashinfo_read_page_offset("/nonexistent", &off), -ENOENT);
}

static void presence_and_free(void)
{
	struct vmcore_info vi = VMCORE_INFO_INIT;

	CHECK(!vmcore_info_present(&vi));
	vi.has_page_offset = true;
	CHECK(vmcore_info_present(&vi));
	vi.has_page_offset = false;
	vi.vmcoreinfo.size = 0x1000;
	CHECK(vmcore_info_present(&vi));
	vi.vmcoreinfo.size = 0;
	rangelist_add(&vi.cpu_notes, 0x1000, 0x400);
	CHECK(vmcore_info_present(&vi));
	vmcore_info_free(&vi);
	CHECK(!vmcore_info_present(&vi));
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(reads_vmcoreinfo_address_and_size);
	RUN(reads_crash_notes_in_cpu_order);
	RUN(cpus_without_notes_are_enoent);
	RUN(page_offset_comes_from_the_direct_map_segment);
	RUN(presence_and_free);
})

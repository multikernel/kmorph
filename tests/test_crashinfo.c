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

static void check_page_offset(const char *rel, const Elf64_Phdr *ph, size_t n,
			      int want_ret, uint64_t want)
{
	char path[512];
	uint64_t off = 0;

	put_kcore(rel, ph, n);
	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	CHECK_EQ(crashinfo_read_page_offset(path, &off), want_ret);
	if (!want_ret)
		CHECK_EQ(off, want);
}

static void load(Elf64_Phdr *ph, uint64_t vaddr, uint64_t paddr, uint64_t size)
{
	memset(ph, 0, sizeof(*ph));
	ph->p_type = PT_LOAD;
	ph->p_vaddr = vaddr;
	ph->p_paddr = paddr;
	ph->p_memsz = size;
}

static void page_offset_on_x86_64(void)
{
	Elf64_Phdr ph[4];

	memset(ph, 0, sizeof(ph));
	ph[0].p_type = PT_NOTE;
	load(&ph[1], 0xffffffff81000000ULL, 0x1000000, 0x2000000);	/* kernel image */
	load(&ph[2], 0xffffc90000000000ULL, (Elf64_Addr)-1, 0x1000);	/* vmalloc */
	load(&ph[3], 0xffff888000100000ULL, 0x100000, 0x7ff00000);	/* direct map */
	check_page_offset("kcore-x86", ph, 4, 0, 0xffff888000000000ULL);
}

/* The image sits in the vmalloc area and comes first, the linear map below */
static void page_offset_on_arm64(void)
{
	Elf64_Phdr ph[4];

	memset(ph, 0, sizeof(ph));
	ph[0].p_type = PT_NOTE;
	load(&ph[1], 0xffff800080000000ULL, 0x40200000, 0x2000000);	/* kernel image */
	load(&ph[2], 0xffff800000000000ULL, (Elf64_Addr)-1, 0x8000000);	/* modules */
	load(&ph[3], 0xfff0000000000000ULL, 0x40000000, 0x100000000ULL);	/* linear map */
	check_page_offset("kcore-arm64", ph, 4, 0, 0xfff0000000000000ULL - 0x40000000);
}

static void page_offset_needs_the_image_in_ram(void)
{
	char path[512];
	Elf64_Phdr ph[4];
	uint64_t off = 0;

	memset(ph, 0, sizeof(ph));
	ph[0].p_type = PT_NOTE;
	load(&ph[1], 0xffffffff81000000ULL, 0x1000000, 0x2000000);
	load(&ph[2], 0xffffc90000000000ULL, (Elf64_Addr)-1, 0x1000);
	/* RAM that does not hold the image */
	load(&ph[3], 0xffff888100000000ULL, 0x100000000ULL, 0x40000000);
	check_page_offset("kcore-noimage", ph, 4, -ENOENT, 0);
	check_page_offset("kcore-nodm", ph, 3, -ENOENT, 0);
	check_page_offset("kcore-empty", ph, 0, -ENOENT, 0);

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
	RUN(page_offset_on_x86_64);
	RUN(page_offset_on_arm64);
	RUN(page_offset_needs_the_image_in_ram);
	RUN(presence_and_free);
})

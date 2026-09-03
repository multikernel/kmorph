#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/dump.h"
#include "kmorph/file.h"

static char dir[] = "/tmp/kmorph-test-dump-XXXXXX";
static char mem[256];

/*
 * Fake /dev/mem, 3 pages: page 0 holds a CPU note buffer at 0x100 and a
 * VMCOREINFO buffer at 0x800, page 1 is all zero, page 2 is 0x33.
 */
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

static size_t note_len, info_len;

static void put_mem(void)
{
	static unsigned char image[0x3000];

	memset(image, 0, sizeof(image));
	note_len = make_note(image + 0x100, NT_PRSTATUS, "CORE", 8);
	info_len = make_note(image + 0x800, 0, "VMCOREINFO", 16);
	memset(image + 0x2000, 0x33, 0x1000);
	CHECK_EQ(file_write(mem, image, sizeof(image)), 0);
}

static void fill_info(struct vmcore_info *vi)
{
	vi->has_page_offset = true;
	vi->page_offset = 0xffff888000000000ULL;
	vi->vmcoreinfo.base = 0x800;
	vi->vmcoreinfo.size = 0x400;
	rangelist_add(&vi->cpu_notes, 0x100, 0x400);	/* CPU 0: a saved note */
	rangelist_add(&vi->cpu_notes, 0, 0);		/* CPU 1: no buffer */
	rangelist_add(&vi->cpu_notes, 0x1000, 0x400);	/* CPU 2: buffer never written */
}

static void writes_a_core_with_notes_and_holes(void)
{
	char out[256];
	struct rangeset ranges = RANGESET_INIT;
	struct vmcore_info vi = VMCORE_INFO_INIT;
	struct dump_stats st;
	struct stat sb;
	unsigned char *data;
	size_t len;
	Elf64_Ehdr *eh;
	Elf64_Phdr *ph;

	snprintf(out, sizeof(out), "%s/core", dir);
	fill_info(&vi);
	/* one range spanning all 3 pages: notes, an all-zero page, then 0x33 */
	rangeset_add(&ranges, 0x0, 0x3000);

	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, out, &st), 0);
	CHECK_EQ(st.cpu_notes, 1);
	CHECK(st.vmcoreinfo);

	CHECK_EQ(file_read(out, (void **)&data, &len), 0);
	eh = (Elf64_Ehdr *)data;
	CHECK_EQ(memcmp(eh->e_ident, ELFMAG, SELFMAG), 0);
	CHECK_EQ(eh->e_type, ET_CORE);
	CHECK_EQ(eh->e_phnum, 2);
	ph = (Elf64_Phdr *)(data + eh->e_phoff);
	CHECK_EQ(ph[0].p_type, PT_NOTE);
	CHECK_EQ(ph[0].p_filesz, note_len + info_len);
	CHECK_EQ(memcmp(data + ph[0].p_offset + 12, "CORE", 5), 0);
	CHECK_EQ(memcmp(data + ph[0].p_offset + note_len + 12, "VMCOREINFO", 11), 0);
	CHECK_EQ(ph[1].p_paddr, 0);
	CHECK_EQ(ph[1].p_vaddr, 0xffff888000000000ULL);
	CHECK_EQ(len, ph[1].p_offset + 0x3000);
	CHECK_EQ(data[ph[1].p_offset + 0x100 + 12], 'C');
	CHECK_EQ(data[ph[1].p_offset + 0x1000], 0);
	CHECK_EQ(data[ph[1].p_offset + 0x2000], 0x33);
	CHECK_EQ(data[ph[1].p_offset + 0x2fff], 0x33);
	free(data);

	/* the all-zero middle page is a hole */
	CHECK_EQ(stat(out, &sb), 0);
	CHECK(sb.st_blocks * 512 < (off_t)len);
	rangeset_free(&ranges);
	vmcore_info_free(&vi);
}

static void zero_ranges_leave_holes_but_a_full_size_file(void)
{
	char out[256];
	struct rangeset ranges = RANGESET_INIT;
	struct vmcore_info vi = VMCORE_INFO_INIT;
	struct stat sb;

	snprintf(out, sizeof(out), "%s/zero", dir);
	rangeset_add(&ranges, 0x1000, 0x1000);
	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, out, NULL), 0);
	CHECK_EQ(stat(out, &sb), 0);
	CHECK_EQ(sb.st_size, 0x2000);
	CHECK(sb.st_blocks * 512 <= 0x1000);
	rangeset_free(&ranges);
}

static void a_note_past_the_end_of_mem_is_not_fatal(void)
{
	char out[256];
	struct rangeset ranges = RANGESET_INIT;
	struct vmcore_info vi = VMCORE_INFO_INIT;
	struct dump_stats st;

	snprintf(out, sizeof(out), "%s/note-oob", dir);
	fill_info(&vi);
	rangelist_add(&vi.cpu_notes, 0x10000, 0x400);	/* past the end of the fake mem */
	rangeset_add(&ranges, 0x0, 0x3000);

	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, out, &st), 0);
	CHECK_EQ(st.cpu_notes, 1);	/* only CPU 0's real note, not the out-of-range one */

	rangeset_free(&ranges);
	vmcore_info_free(&vi);
}

static void a_fifo_receives_the_same_bytes_sequentially(void)
{
	char out[256], fifo[256], drained[256];
	struct rangeset ranges = RANGESET_INIT;
	struct vmcore_info vi = VMCORE_INFO_INIT;
	void *a, *b;
	size_t la, lb;
	pid_t pid;
	int status;

	snprintf(out, sizeof(out), "%s/core-file", dir);
	snprintf(fifo, sizeof(fifo), "%s/core-fifo", dir);
	snprintf(drained, sizeof(drained), "%s/core-drained", dir);
	fill_info(&vi);
	/* non-page-multiple sizes so the FIFO path both fills a gap and pads the tail */
	rangeset_add(&ranges, 0x0, 0x1800);
	rangeset_add(&ranges, 0x2000, 0x800);
	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, out, NULL), 0);

	CHECK_EQ(mkfifo(fifo, 0600), 0);
	pid = fork();
	CHECK(pid >= 0);
	if (pid < 0) {
		rangeset_free(&ranges);
		vmcore_info_free(&vi);
		return;
	}
	if (pid == 0) {
		char buf[4096];
		int in = open(fifo, O_RDONLY);
		int dst = open(drained, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		ssize_t n;

		while ((n = read(in, buf, sizeof(buf))) > 0)
			if (write(dst, buf, n) != n)
				_exit(2);
		_exit(in < 0 || dst < 0 ? 1 : 0);
	}
	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, fifo, NULL), 0);
	CHECK_EQ(waitpid(pid, &status, 0), pid);
	CHECK_EQ(WEXITSTATUS(status), 0);

	CHECK_EQ(file_read(out, &a, &la), 0);
	CHECK_EQ(file_read(drained, &b, &lb), 0);
	CHECK_EQ(la, lb);
	CHECK_EQ(memcmp(a, b, la), 0);
	free(a);
	free(b);
	rangeset_free(&ranges);
	vmcore_info_free(&vi);
}

static void short_memory_fails_with_the_errno(void)
{
	char out[256];
	struct rangeset ranges = RANGESET_INIT;
	struct vmcore_info vi = VMCORE_INFO_INIT;

	snprintf(out, sizeof(out), "%s/short", dir);
	rangeset_add(&ranges, 0x2000, 0x2000);	/* runs past the 0x3000 image */
	CHECK_EQ(dump_vmcore(mem, &ranges, &vi, out, NULL), -EIO);
	CHECK(dump_vmcore("/nonexistent/mem", &ranges, &vi, out, NULL) < 0);
	rangeset_free(&ranges);
}

TEST_MAIN({
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	snprintf(mem, sizeof(mem), "%s/mem", dir);
	put_mem();
	RUN(writes_a_core_with_notes_and_holes);
	RUN(zero_ranges_leave_holes_but_a_full_size_file);
	RUN(a_note_past_the_end_of_mem_is_not_fatal);
	RUN(a_fifo_receives_the_same_bytes_sequentially);
	RUN(short_memory_fails_with_the_errno);
})

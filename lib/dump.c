#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kmorph/dump.h"
#include "kmorph/elfcore.h"

#define DUMP_CHUNK (1 << 20)
/* r->size comes from a device-tree reg cell; cap it against a garbled tree. */
#define DUMP_NOTE_MAX (1 << 20)

/*
 * read(2) on /dev/mem refuses addresses above this kernel's own
 * high_memory, and the predecessor's memory lies mostly above it;
 * mmap(2) has no such limit. A regular file (the tests' fake /dev/mem)
 * is bounded by its size instead, so a range past its end reads short.
 */
struct source {
	int fd;
	uint64_t limit;		/* bytes readable; UINT64_MAX for a device */
};

static int source_open(const char *path, struct source *src)
{
	struct stat st;

	src->fd = open(path, O_RDONLY | O_CLOEXEC);
	if (src->fd < 0)
		return -errno;
	if (fstat(src->fd, &st) < 0) {
		int err = errno;

		close(src->fd);
		return -err;
	}
	src->limit = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : UINT64_MAX;
	return 0;
}

static int source_read(const struct source *src, void *buf, size_t len, uint64_t off)
{
	uint64_t start = off & ~(ELFCORE_PAGE - 1);
	size_t maplen = (size_t)(off - start) + len;
	void *map;

	if (off > src->limit || len > src->limit - off)
		return -EIO;
	map = mmap(NULL, maplen, PROT_READ, MAP_SHARED, src->fd, start);
	if (map == MAP_FAILED)
		return -errno;
	memcpy(buf, (const char *)map + (off - start), len);
	munmap(map, maplen);
	return 0;
}

struct sink {
	int fd;
	bool seekable;
	uint64_t pos;		/* sequential output: bytes written so far */
};

static const unsigned char zero_page[ELFCORE_PAGE];

static int write_all(int fd, const void *buf, size_t len, uint64_t off, bool seekable)
{
	const char *p = buf;

	while (len) {
		ssize_t n = seekable ? pwrite(fd, p, len, off) : write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!n)
			return -EIO;
		p += n;
		off += n;
		len -= n;
	}
	return 0;
}

/* A gap on sequential output has to be filled, it cannot be seeked over. */
static int sink_write(struct sink *s, const void *buf, size_t len, uint64_t off)
{
	int ret;

	if (!s->seekable) {
		while (s->pos < off) {
			size_t gap = off - s->pos < sizeof(zero_page) ? off - s->pos : sizeof(zero_page);

			ret = write_all(s->fd, zero_page, gap, 0, false);
			if (ret)
				return ret;
			s->pos += gap;
		}
		s->pos += len;
	}
	return write_all(s->fd, buf, len, off, s->seekable);
}

static bool page_is_zero(const unsigned char *p)
{
	return !memcmp(p, zero_page, sizeof(zero_page));
}

/* Write the non-zero pages of a chunk as runs; the rest stays a hole. */
static int write_chunk(struct sink *s, const unsigned char *buf, size_t len, uint64_t off)
{
	size_t start = 0, i;

	if (!s->seekable)
		return sink_write(s, buf, len, off);
	for (i = 0; i < len; i += ELFCORE_PAGE) {
		size_t page = len - i < ELFCORE_PAGE ? len - i : ELFCORE_PAGE;
		bool zero = page == ELFCORE_PAGE && page_is_zero(buf + i);

		if (zero && i > start) {
			int ret = sink_write(s, buf + start, i - start, off + start);

			if (ret)
				return ret;
		}
		if (zero)
			start = i + page;
	}
	return start < len ? sink_write(s, buf + start, len - start, off + start) : 0;
}

static int copy_range(const struct source *src, struct sink *s, unsigned char *buf,
		      uint64_t base, uint64_t size, uint64_t off)
{
	while (size) {
		size_t want = size < DUMP_CHUNK ? size : DUMP_CHUNK;
		int ret = source_read(src, buf, want, base);

		if (ret)
			return ret;
		ret = write_chunk(s, buf, want, off);
		if (ret)
			return ret;
		base += want;
		off += want;
		size -= want;
	}
	return 0;
}

/* The note records a buffer holds, or none; a buffer never written is empty. */
static int read_note(const struct source *src, const struct range *r, struct elfcore_note *note)
{
	unsigned char *buf;

	note->data = NULL;
	note->len = 0;
	if (!r->size || r->size > DUMP_NOTE_MAX)
		return 0;
	buf = malloc(r->size);
	if (!buf)
		return -ENOMEM;
	if (source_read(src, buf, r->size, r->base)) {
		free(buf);
		return 0;
	}
	note->len = elfcore_notes_len(buf, r->size);
	if (!note->len) {
		free(buf);
		return 0;
	}
	note->data = buf;
	return 0;
}

static int collect_notes(const struct source *src, const struct vmcore_info *vi,
			 struct elfcore_note **out, size_t *count, struct dump_stats *stats)
{
	struct elfcore_note *notes, cur;
	size_t i, n = 0;
	int ret;

	notes = calloc(vi->cpu_notes.count + 1, sizeof(*notes));
	if (!notes)
		return -ENOMEM;
	for (i = 0; i < vi->cpu_notes.count; i++) {
		ret = read_note(src, &vi->cpu_notes.r[i], &cur);
		if (ret)
			goto fail;
		if (cur.len)
			notes[n++] = cur;
	}
	stats->cpu_notes = n;
	ret = read_note(src, &vi->vmcoreinfo, &cur);
	if (ret)
		goto fail;
	if (cur.len) {
		notes[n++] = cur;
		stats->vmcoreinfo = true;
	}
	*out = notes;
	*count = n;
	return 0;
fail:
	for (i = 0; i < n; i++)
		free((void *)notes[i].data);
	free(notes);
	return ret;
}

static void free_notes(struct elfcore_note *notes, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		free((void *)notes[i].data);
	free(notes);
}

static int open_sink(const char *path, uint64_t total, struct sink *s)
{
	struct stat st;

	s->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (s->fd < 0)
		return -errno;
	if (fstat(s->fd, &st) < 0)
		goto fail;
	s->seekable = S_ISREG(st.st_mode);
	s->pos = 0;
	if (s->seekable && ftruncate(s->fd, total) < 0)
		goto fail;
	return 0;
fail:
	{
		int err = errno;

		close(s->fd);
		return -err;
	}
}

int dump_vmcore(const char *mem_path, const struct rangeset *ranges,
		const struct vmcore_info *vi, const char *out_path,
		struct dump_stats *stats)
{
	struct dump_stats local;
	struct elfcore_note *notes = NULL;
	struct elfcore_layout layout;
	struct sink s;
	struct source src;
	unsigned char *buf = NULL;
	size_t nnotes = 0, i;
	int ret;

	if (!stats)
		stats = &local;
	memset(stats, 0, sizeof(*stats));

	ret = source_open(mem_path, &src);
	if (ret)
		return ret;
	ret = collect_notes(&src, vi, &notes, &nnotes, stats);
	if (ret) {
		close(src.fd);
		return ret;
	}
	ret = elfcore_layout(ranges, notes, nnotes, vi->has_page_offset, vi->page_offset, &layout);
	free_notes(notes, nnotes);
	if (ret) {
		close(src.fd);
		return ret;
	}

	ret = open_sink(out_path, layout.total, &s);
	if (ret)
		goto out;
	buf = malloc(DUMP_CHUNK);
	if (!buf) {
		ret = -ENOMEM;
		goto close_sink;
	}
	ret = sink_write(&s, layout.header, layout.header_len, 0);
	for (i = 0; i < ranges->count && !ret; i++)
		ret = copy_range(&src, &s, buf, ranges->r[i].base, ranges->r[i].size,
				 layout.offsets[i]);
	if (!ret && !s.seekable && s.pos < layout.total)
		ret = sink_write(&s, buf, 0, layout.total);	/* trailing padding */
	if (!ret && s.seekable && fsync(s.fd) < 0)
		ret = -errno;
close_sink:
	if (close(s.fd) < 0 && !ret)
		ret = -errno;
out:
	free(buf);
	elfcore_layout_free(&layout);
	close(src.fd);
	return ret;
}

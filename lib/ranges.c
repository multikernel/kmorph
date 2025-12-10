#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/ranges.h"

static uint64_t range_end(const struct range *r)
{
	return r->base + r->size;
}

static int rangeset_insert(struct rangeset *s, size_t at, uint64_t base, uint64_t size)
{
	if (s->count == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 8;
		struct range *r = realloc(s->r, cap * sizeof(*r));

		if (!r)
			return -ENOMEM;
		s->r = r;
		s->cap = cap;
	}
	memmove(&s->r[at + 1], &s->r[at], (s->count - at) * sizeof(*s->r));
	s->r[at].base = base;
	s->r[at].size = size;
	s->count++;
	return 0;
}

static void rangeset_remove(struct rangeset *s, size_t at)
{
	memmove(&s->r[at], &s->r[at + 1], (s->count - at - 1) * sizeof(*s->r));
	s->count--;
}

int rangeset_add(struct rangeset *s, uint64_t base, uint64_t size)
{
	uint64_t end = base + size;
	size_t i = 0;

	if (!size || end < base)
		return -EINVAL;

	while (i < s->count && range_end(&s->r[i]) < base)
		i++;

	/* Swallow every range touching [base, end) into it, then insert once. */
	while (i < s->count && s->r[i].base <= end) {
		if (s->r[i].base < base)
			base = s->r[i].base;
		if (range_end(&s->r[i]) > end)
			end = range_end(&s->r[i]);
		rangeset_remove(s, i);
	}

	return rangeset_insert(s, i, base, end - base);
}

int rangeset_subtract(struct rangeset *s, uint64_t base, uint64_t size)
{
	uint64_t end = base + size;
	size_t i = 0;

	if (!size || end < base)
		return -EINVAL;

	while (i < s->count) {
		struct range *r = &s->r[i];
		uint64_t rend = range_end(r);

		if (rend <= base || r->base >= end) {
			i++;
			continue;
		}
		if (r->base < base && rend > end) {
			int ret = rangeset_insert(s, i + 1, end, rend - end);

			if (ret)
				return ret;
			s->r[i].size = base - s->r[i].base;
			return 0;
		}
		if (r->base < base) {
			r->size = base - r->base;
			i++;
		} else if (rend > end) {
			r->size = rend - end;
			r->base = end;
			i++;
		} else {
			rangeset_remove(s, i);
		}
	}
	return 0;
}

int rangeset_subtract_set(struct rangeset *s, const struct rangeset *minus)
{
	size_t i;

	for (i = 0; i < minus->count; i++) {
		int ret = rangeset_subtract(s, minus->r[i].base, minus->r[i].size);

		if (ret)
			return ret;
	}
	return 0;
}

int rangeset_copy(struct rangeset *dst, const struct rangeset *src)
{
	size_t i;

	rangeset_free(dst);
	for (i = 0; i < src->count; i++) {
		int ret = rangeset_add(dst, src->r[i].base, src->r[i].size);

		if (ret)
			return ret;
	}
	return 0;
}

int rangeset_align(const struct rangeset *s, uint64_t block, struct rangeset *out)
{
	size_t i;

	if (!block)
		return -EINVAL;
	for (i = 0; i < s->count; i++) {
		uint64_t base = (s->r[i].base + block - 1) / block * block;
		uint64_t end = range_end(&s->r[i]) / block * block;
		int ret;

		if (end <= base)
			continue;
		ret = rangeset_add(out, base, end - base);
		if (ret)
			return ret;
	}
	return 0;
}

bool rangeset_contains(const struct rangeset *s, uint64_t base, uint64_t size)
{
	size_t i;

	for (i = 0; i < s->count; i++)
		if (s->r[i].base <= base && base + size <= range_end(&s->r[i]))
			return true;
	return false;
}

uint64_t rangeset_total(const struct rangeset *s)
{
	uint64_t total = 0;
	size_t i;

	for (i = 0; i < s->count; i++)
		total += s->r[i].size;
	return total;
}

void rangeset_free(struct rangeset *s)
{
	free(s->r);
	s->r = NULL;
	s->count = 0;
	s->cap = 0;
}

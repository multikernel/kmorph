#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/parse.h"

static int parse_u64_prefix(const char *s, uint64_t *v, const char **end)
{
	char *e;

	if (!isdigit((unsigned char)*s))
		return -EINVAL;
	errno = 0;
	*v = strtoull(s, &e, 10);
	if (errno)
		return -ERANGE;
	*end = e;
	return 0;
}

int parse_size(const char *s, uint64_t *bytes)
{
	const char *unit;
	uint64_t v;
	int shift, ret;

	ret = parse_u64_prefix(s, &v, &unit);
	if (ret)
		return ret;

	switch (toupper((unsigned char)unit[0])) {
	case '\0': shift = 0; break;
	case 'K': shift = 10; break;
	case 'M': shift = 20; break;
	case 'G': shift = 30; break;
	case 'T': shift = 40; break;
	default: return -EINVAL;
	}
	if (shift) {
		unit++;
		if (toupper((unsigned char)*unit) == 'B')
			unit++;
	}
	if (*unit)
		return -EINVAL;
	if (shift && v > (UINT64_MAX >> shift))
		return -ERANGE;

	*bytes = v << shift;
	return 0;
}

int parse_duration_ms(const char *s, uint64_t *ms)
{
	const char *unit;
	uint64_t v, scale;
	int ret;

	ret = parse_u64_prefix(s, &v, &unit);
	if (ret)
		return ret;

	if (!strcmp(unit, "") || !strcmp(unit, "ms"))
		scale = 1;
	else if (!strcmp(unit, "s"))
		scale = 1000;
	else if (!strcmp(unit, "m"))
		scale = 60 * 1000;
	else
		return -EINVAL;
	if (v > UINT64_MAX / scale)
		return -ERANGE;

	*ms = v * scale;
	return 0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

static int cpulist_push(struct cpulist *l, size_t *cap, uint64_t id)
{
	if (l->count == *cap) {
		size_t n = *cap ? *cap * 2 : 16;
		uint64_t *ids = realloc(l->ids, n * sizeof(*ids));

		if (!ids)
			return -ENOMEM;
		l->ids = ids;
		*cap = n;
	}
	l->ids[l->count++] = id;
	return 0;
}

void cpulist_normalize(struct cpulist *l)
{
	size_t i, n = 0;

	if (!l->count)
		return;
	qsort(l->ids, l->count, sizeof(*l->ids), cmp_u64);
	for (i = 0; i < l->count; i++)
		if (n == 0 || l->ids[n - 1] != l->ids[i])
			l->ids[n++] = l->ids[i];
	l->count = n;
}

int parse_cpulist(const char *s, struct cpulist *out)
{
	size_t cap = 0;
	int ret;

	out->ids = NULL;
	out->count = 0;
	if (!*s)
		return -EINVAL;

	while (*s) {
		uint64_t lo, hi, id;
		const char *end;

		ret = parse_u64_prefix(s, &lo, &end);
		if (ret)
			goto fail;
		hi = lo;
		if (*end == '-') {
			ret = parse_u64_prefix(end + 1, &hi, &end);
			if (ret)
				goto fail;
			if (hi < lo) {
				ret = -EINVAL;
				goto fail;
			}
		}
		for (id = lo; ; id++) {
			ret = cpulist_push(out, &cap, id);
			if (ret)
				goto fail;
			if (id == hi)
				break;
		}
		if (*end == ',')
			end++;
		else if (*end) {
			ret = -EINVAL;
			goto fail;
		}
		s = end;
	}

	cpulist_normalize(out);
	return 0;
fail:
	cpulist_free(out);
	return ret;
}

bool cpulist_has(const struct cpulist *l, uint64_t id)
{
	size_t i;

	for (i = 0; i < l->count; i++)
		if (l->ids[i] == id)
			return true;
	return false;
}

int parse_strlist(const char *s, struct strlist *out)
{
	const char *p = s;
	size_t cap = 0;

	out->items = NULL;
	out->count = 0;
	if (!*s)
		return -EINVAL;

	while (*p) {
		const char *start, *end;
		char *item;

		while (isspace((unsigned char)*p))
			p++;
		start = p;
		while (*p && *p != ',')
			p++;
		end = p;
		while (end > start && isspace((unsigned char)end[-1]))
			end--;
		if (end == start)
			goto fail;

		if (out->count == cap) {
			size_t n = cap ? cap * 2 : 4;
			char **items = realloc(out->items, n * sizeof(*items));

			if (!items)
				goto fail;
			out->items = items;
			cap = n;
		}
		item = strndup(start, end - start);
		if (!item)
			goto fail;
		out->items[out->count++] = item;
		if (*p == ',')
			p++;
	}
	return 0;
fail:
	strlist_free(out);
	return -EINVAL;
}

void strlist_free(struct strlist *l)
{
	size_t i;

	for (i = 0; i < l->count; i++)
		free(l->items[i]);
	free(l->items);
	l->items = NULL;
	l->count = 0;
}

void cpulist_free(struct cpulist *l)
{
	free(l->ids);
	l->ids = NULL;
	l->count = 0;
}

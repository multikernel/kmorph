#ifndef KMORPH_RANGES_H
#define KMORPH_RANGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct range {
	uint64_t base;
	uint64_t size;
};

/* Sorted, non-overlapping, non-adjacent ranges. */
struct rangeset {
	struct range *r;
	size_t count;
	size_t cap;
};

#define RANGESET_INIT { NULL, 0, 0 }

int rangeset_add(struct rangeset *s, uint64_t base, uint64_t size);
int rangeset_subtract(struct rangeset *s, uint64_t base, uint64_t size);
int rangeset_subtract_set(struct rangeset *s, const struct rangeset *minus);
int rangeset_copy(struct rangeset *dst, const struct rangeset *src);
/* The whole blocks of size block inside s, into out. */
int rangeset_align(const struct rangeset *s, uint64_t block, struct rangeset *out);
bool rangeset_contains(const struct rangeset *s, uint64_t base, uint64_t size);
uint64_t rangeset_total(const struct rangeset *s);
void rangeset_free(struct rangeset *s);

#endif

#ifndef KMORPH_PARSE_H
#define KMORPH_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cpulist {
	uint64_t *ids;
	size_t count;
};

struct strlist {
	char **items;
	size_t count;
};

int parse_size(const char *s, uint64_t *bytes);
int parse_duration_ms(const char *s, uint64_t *ms);
int parse_cpulist(const char *s, struct cpulist *out);
bool cpulist_has(const struct cpulist *l, uint64_t id);
/* Sort ascending and drop duplicates. */
void cpulist_normalize(struct cpulist *l);
void cpulist_free(struct cpulist *l);

/* Comma-separated words, surrounding whitespace dropped, empty words rejected. */
int parse_strlist(const char *s, struct strlist *out);
void strlist_free(struct strlist *l);

#endif

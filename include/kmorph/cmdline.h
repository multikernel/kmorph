#ifndef KMORPH_CMDLINE_H
#define KMORPH_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

/* A kernel command line as its words; a word's name is what precedes its '='. */
struct cmdline {
	char **words;
	size_t count;
};

/* Splits at blanks outside double quotes, as the kernel does. */
int cmdline_split(const char *s, struct cmdline *out);
void cmdline_free(struct cmdline *c);

/* name may be a whole word, of which only the name counts. */
bool cmdline_has(const struct cmdline *c, const char *name);
void cmdline_remove(struct cmdline *c, const char *name);
int cmdline_append(struct cmdline *c, const char *word);
/* Appends word unless a word of the same name is there. */
int cmdline_add(struct cmdline *c, const char *word);

/* The words joined by spaces; NULL when there are none or on ENOMEM. */
char *cmdline_join(const struct cmdline *c);

#endif

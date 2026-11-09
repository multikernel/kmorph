#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/cmdline.h"

static bool is_blank(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n';
}

static bool has_name(const char *word, const char *name)
{
	size_t n = strcspn(name, "=");

	return !strncmp(word, name, n) && (word[n] == '\0' || word[n] == '=');
}

int cmdline_append(struct cmdline *c, const char *word)
{
	char **words = realloc(c->words, (c->count + 1) * sizeof(*words));

	if (!words)
		return -ENOMEM;
	c->words = words;
	c->words[c->count] = strdup(word);
	if (!c->words[c->count])
		return -ENOMEM;
	c->count++;
	return 0;
}

int cmdline_split(const char *s, struct cmdline *out)
{
	char *copy = strdup(s), *p, *start;
	bool quoted;
	int ret = 0;

	out->words = NULL;
	out->count = 0;
	if (!copy)
		return -ENOMEM;
	for (p = copy; *p && !ret;) {
		while (is_blank(*p))
			p++;
		if (!*p)
			break;
		start = p;
		for (quoted = false; *p && (quoted || !is_blank(*p)); p++)
			if (*p == '"')
				quoted = !quoted;
		if (*p)
			*p++ = '\0';
		ret = cmdline_append(out, start);
	}
	free(copy);
	if (ret)
		cmdline_free(out);
	return ret;
}

void cmdline_free(struct cmdline *c)
{
	size_t i;

	for (i = 0; i < c->count; i++)
		free(c->words[i]);
	free(c->words);
	c->words = NULL;
	c->count = 0;
}

bool cmdline_has(const struct cmdline *c, const char *name)
{
	size_t i;

	for (i = 0; i < c->count; i++)
		if (has_name(c->words[i], name))
			return true;
	return false;
}

void cmdline_remove(struct cmdline *c, const char *name)
{
	size_t i, kept = 0;

	for (i = 0; i < c->count; i++) {
		if (has_name(c->words[i], name))
			free(c->words[i]);
		else
			c->words[kept++] = c->words[i];
	}
	c->count = kept;
}

int cmdline_add(struct cmdline *c, const char *word)
{
	size_t n = strcspn(word, "=");
	char name[128];

	if (n >= sizeof(name))
		return -ENAMETOOLONG;
	memcpy(name, word, n);
	name[n] = '\0';
	return cmdline_has(c, name) ? 0 : cmdline_append(c, word);
}

char *cmdline_join(const struct cmdline *c)
{
	size_t i, len = 0;
	char *out, *p;

	if (!c->count)
		return NULL;
	for (i = 0; i < c->count; i++)
		len += strlen(c->words[i]) + 1;
	out = malloc(len);
	if (!out)
		return NULL;
	for (p = out, i = 0; i < c->count; i++) {
		size_t n = strlen(c->words[i]);

		memcpy(p, c->words[i], n);
		p += n;
		*p++ = i + 1 < c->count ? ' ' : '\0';
	}
	return out;
}

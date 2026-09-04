#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/config.h"
#include "kmorph/file.h"

static void config_defaults(struct kmorph_config *c)
{
	memset(c, 0, sizeof(*c));
	c->name = strdup("successor");
	c->probe_interval_ms = 100;
	c->probe_timeouts = 5;
	c->fence_retries = 3;
	c->console_baud = 115200;
}

static int set_string(char **dst, const char *value)
{
	free(*dst);
	*dst = NULL;
	if (!*value)
		return 0;
	*dst = strdup(value);
	return *dst ? 0 : -ENOMEM;
}

static int set_uint(unsigned int *dst, const char *value, unsigned int min)
{
	char *end;
	unsigned long v;

	if (!isdigit((unsigned char)*value))
		return -EINVAL;
	v = strtoul(value, &end, 10);
	if (*end || v < min || v > UINT32_MAX)
		return -EINVAL;
	*dst = v;
	return 0;
}

static int config_set(struct kmorph_config *c, const char *key, const char *value)
{
	if (!strcmp(key, "name"))
		return *value ? set_string(&c->name, value) : -EINVAL;
	if (!strcmp(key, "cpus")) {
		cpulist_free(&c->cpus);
		return *value ? parse_cpulist(value, &c->cpus) : 0;
	}
	if (!strcmp(key, "memory"))
		return parse_size(value, &c->memory);
	if (!strcmp(key, "kernel"))
		return set_string(&c->kernel, value);
	if (!strcmp(key, "initrd"))
		return set_string(&c->initrd, value);
	if (!strcmp(key, "cmdline"))
		return set_string(&c->cmdline, value);
	if (!strcmp(key, "machine_cpus")) {
		cpulist_free(&c->machine_cpus);
		return *value ? parse_cpulist(value, &c->machine_cpus) : 0;
	}
	if (!strcmp(key, "devices")) {
		strlist_free(&c->devices);
		return *value ? parse_strlist(value, &c->devices) : 0;
	}
	if (!strcmp(key, "modules")) {
		strlist_free(&c->modules);
		return *value ? parse_strlist(value, &c->modules) : 0;
	}
	if (!strcmp(key, "probe_interval"))
		return parse_duration_ms(value, &c->probe_interval_ms);
	if (!strcmp(key, "probe_timeouts"))
		return set_uint(&c->probe_timeouts, value, 1);
	if (!strcmp(key, "fence_retries"))
		return set_uint(&c->fence_retries, value, 0);
	if (!strcmp(key, "dump"))
		return set_string(&c->dump, value);
	if (!strcmp(key, "console"))
		return set_string(&c->console, value);
	if (!strcmp(key, "console_baud"))
		return set_uint(&c->console_baud, value, 1);
	if (!strcmp(key, "console_login"))
		return set_string(&c->console_login, value);
	return -ENOENT;
}

static char *trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

static int config_parse_line(struct kmorph_config *c, char *line, int lineno,
			     char *err, size_t errlen)
{
	char *key, *value, *eq;
	int ret;

	line = trim(line);
	if (!*line || *line == '#')
		return 0;

	eq = strchr(line, '=');
	if (!eq) {
		snprintf(err, errlen, "line %d: expected key = value", lineno);
		return -EINVAL;
	}
	*eq = '\0';
	key = trim(line);
	value = trim(eq + 1);

	ret = config_set(c, key, value);
	if (ret == -ENOENT)
		snprintf(err, errlen, "line %d: unknown key '%s'", lineno, key);
	else if (ret)
		snprintf(err, errlen, "line %d: bad value for '%s': %s",
			 lineno, key, strerror(-ret));
	return ret;
}

int config_parse(const char *text, struct kmorph_config *c, char *err, size_t errlen)
{
	char *copy, *line, *next;
	int lineno = 0, ret = 0;

	config_defaults(c);
	if (!c->name)
		return -ENOMEM;
	if (errlen)
		err[0] = '\0';

	copy = strdup(text);
	if (!copy)
		return -ENOMEM;

	for (line = copy; line && !ret; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		ret = config_parse_line(c, line, ++lineno, err, errlen);
	}

	free(copy);
	if (!ret && c->console && !c->console_login) {
		snprintf(err, errlen, "console needs console_login");
		ret = -EINVAL;
	}
	if (!ret) {
		c->text = strdup(text);
		if (!c->text)
			ret = -ENOMEM;
	}
	if (ret)
		config_free(c);
	return ret;
}

int config_load(const char *path, struct kmorph_config *c, char *err, size_t errlen)
{
	char *text;
	int ret;

	ret = file_read_string(path, &text);
	if (ret) {
		snprintf(err, errlen, "%s: %s", path, strerror(-ret));
		return ret;
	}
	ret = config_parse(text, c, err, errlen);
	free(text);
	return ret;
}

void config_free(struct kmorph_config *c)
{
	free(c->name);
	cpulist_free(&c->cpus);
	free(c->kernel);
	free(c->initrd);
	free(c->cmdline);
	strlist_free(&c->devices);
	cpulist_free(&c->machine_cpus);
	strlist_free(&c->modules);
	free(c->dump);
	free(c->console);
	free(c->console_login);
	free(c->text);
	memset(c, 0, sizeof(*c));
}

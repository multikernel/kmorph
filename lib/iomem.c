#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/file.h"
#include "kmorph/iomem.h"

static int iomem_parse_line(const char *line, uint64_t *start, uint64_t *end,
			    const char **name)
{
	char *p;

	while (*line == ' ')
		line++;
	if (!isxdigit((unsigned char)*line))
		return -EINVAL;
	*start = strtoull(line, &p, 16);
	if (*p != '-')
		return -EINVAL;
	*end = strtoull(p + 1, &p, 16);
	if (strncmp(p, " : ", 3))
		return -EINVAL;
	*name = p + 3;
	return 0;
}

/* RAM a driver hot-added, adopted RAM included, is "System RAM ($DRIVER)" */
static bool is_system_ram(const char *name)
{
	static const char ram[] = "System RAM";
	size_t len;

	if (strncmp(name, ram, sizeof(ram) - 1))
		return false;
	name += sizeof(ram) - 1;
	len = strlen(name);
	return !len || (!strncmp(name, " (", 2) && name[len - 1] == ')');
}

int iomem_system_ram(const char *path, struct rangeset *ram)
{
	char *text, *line, *next;
	int ret;

	ret = file_read_string(path, &text);
	if (ret)
		return ret;

	for (line = text; line && !ret; line = next) {
		uint64_t start, end;
		const char *name;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		if (iomem_parse_line(line, &start, &end, &name))
			continue;
		if (is_system_ram(name))
			ret = rangeset_add(ram, start, end - start + 1);
	}

	free(text);
	return ret;
}

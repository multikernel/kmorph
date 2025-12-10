#include <ctype.h>
#include <errno.h>
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
		if (!strcmp(name, "System RAM"))
			ret = rangeset_add(ram, start, end - start + 1);
	}

	free(text);
	return ret;
}

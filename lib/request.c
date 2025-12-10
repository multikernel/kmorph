#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "kmorph/request.h"

static size_t line_len(const char *line)
{
	size_t n = strlen(line);

	while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		n--;
	return n;
}

int request_parse(const char *line, enum request_kind *kind)
{
	size_t n = line_len(line);

	if (n == 6 && !strncmp(line, "status", 6))
		*kind = REQUEST_STATUS;
	else if (n == 8 && !strncmp(line, "handover", 8))
		*kind = REQUEST_HANDOVER;
	else
		return -EINVAL;
	return 0;
}

int request_format_status(const struct status_reply *r, char *buf, size_t len)
{
	int n = snprintf(buf, len, "ok %s predecessor=%s last_probe=%llums error=%d\n",
			 r->state, r->predecessor,
			 (unsigned long long)r->last_probe_ms, r->last_error);

	return n >= (int)len ? -ENOSPC : n;
}

int request_parse_status(const char *line, struct status_reply *r)
{
	unsigned long long ms;

	memset(r, 0, sizeof(*r));
	if (sscanf(line, "ok %15s predecessor=%15s last_probe=%llums error=%d",
		   r->state, r->predecessor, &ms, &r->last_error) != 4)
		return -EINVAL;
	r->last_probe_ms = ms;
	return 0;
}

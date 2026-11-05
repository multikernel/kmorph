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

#define NETWORK_MAX (1u << 20)

int request_parse(const char *line, enum request_kind *kind, size_t *len)
{
	size_t n = line_len(line);
	unsigned long payload;
	int end = 0;

	*len = 0;
	if (n == 6 && !strncmp(line, "status", 6))
		*kind = REQUEST_STATUS;
	else if (n == 8 && !strncmp(line, "handover", 8))
		*kind = REQUEST_HANDOVER;
	else if (sscanf(line, "network %lu%n", &payload, &end) == 1 && (size_t)end == n &&
		 payload <= NETWORK_MAX) {
		*kind = REQUEST_NETWORK;
		*len = payload;
	} else
		return -EINVAL;
	return 0;
}

int request_format_network(size_t len, char *buf, size_t buflen)
{
	int n = snprintf(buf, buflen, "network %zu\n", len);

	return n >= (int)buflen ? -ENOSPC : n;
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

static int format_ack(const char *ok, const char *reason, char *buf, size_t len)
{
	int n = reason ? snprintf(buf, len, "refused %s\n", reason) :
			 snprintf(buf, len, "ok %s\n", ok);

	return n >= (int)len ? -ENOSPC : n;
}

static int parse_ack(const char *ok, const char *line, char *reason, size_t len)
{
	size_t n = line_len(line);

	if (n == strlen(ok) + 3 && !strncmp(line, "ok ", 3) && !strncmp(line + 3, ok, n - 3))
		return 0;
	if (n > 8 && !strncmp(line, "refused ", 8)) {
		snprintf(reason, len, "%.*s", (int)(n - 8), line + 8);
		return -EBUSY;
	}
	return -EINVAL;
}

int request_format_handover(const char *reason, char *buf, size_t len)
{
	return format_ack("fencing", reason, buf, len);
}

int request_parse_handover(const char *line, char *reason, size_t len)
{
	return parse_ack("fencing", line, reason, len);
}

int request_format_network_reply(const char *reason, char *buf, size_t len)
{
	return format_ack("network", reason, buf, len);
}

int request_parse_network_reply(const char *line, char *reason, size_t len)
{
	return parse_ack("network", line, reason, len);
}

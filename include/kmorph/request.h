#ifndef KMORPH_REQUEST_H
#define KMORPH_REQUEST_H

#include <stddef.h>
#include <stdint.h>

/*
 * The line protocol between kmorph on the predecessor and kmorphd in the
 * successor, over a vsock stream to the successor's CID.
 */

#define KMORPH_VSOCK_PORT 27501

enum request_kind {
	REQUEST_STATUS,
	REQUEST_HANDOVER,	/* kmorph upgrade: fence me and take over */
	REQUEST_NETWORK,	/* "network <length>", then that many bytes of netsnap text */
};

struct status_reply {
	char state[16];
	char predecessor[16];
	uint64_t last_probe_ms;
	int last_error;
};

/* @len is the length of the payload after the line: 0 but for REQUEST_NETWORK */
int request_parse(const char *line, enum request_kind *kind, size_t *len);
int request_format_network(size_t len, char *buf, size_t buflen);
int request_format_status(const struct status_reply *r, char *buf, size_t len);
int request_parse_status(const char *line, struct status_reply *r);

/* "ok fencing" when @reason is NULL, else "refused <reason>" */
int request_format_handover(const char *reason, char *buf, size_t len);
/* 0 when accepted, -EBUSY with @reason when refused, -EINVAL otherwise */
int request_parse_handover(const char *line, char *reason, size_t len);

/* "ok network" when @reason is NULL, else "refused <reason>" */
int request_format_network_reply(const char *reason, char *buf, size_t len);
int request_parse_network_reply(const char *line, char *reason, size_t len);

#endif

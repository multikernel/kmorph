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
	REQUEST_HANDOVER,	/* reserved for the planned takeover */
};

struct status_reply {
	char state[16];
	char predecessor[16];
	uint64_t last_probe_ms;
	int last_error;
};

int request_parse(const char *line, enum request_kind *kind);
int request_format_status(const struct status_reply *r, char *buf, size_t len);
int request_parse_status(const char *line, struct status_reply *r);

#endif

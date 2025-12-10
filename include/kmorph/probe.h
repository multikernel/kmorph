#ifndef KMORPH_PROBE_H
#define KMORPH_PROBE_H

#include <errno.h>

/*
 * Liveness probe: a vsock connect() to the peer kernel. A live kernel
 * answers RST to an unbound port by itself, so ECONNRESET (or an actual
 * connection) proves it alive. Only a timeout is evidence of death; a
 * local failure says nothing about the peer.
 */

#define PROBE_CID_PREDECESSOR 0
#define PROBE_PORT 1

enum probe_result {
	PROBE_ALIVE,
	PROBE_SILENT,
	PROBE_ERROR,
};

enum probe_result probe_classify(int err);
const char *probe_result_name(enum probe_result r);

/*
 * Start a non-blocking connect. Returns the socket, or -errno when the
 * connect failed at once (classify -ret). Wait for the socket to become
 * writable, then probe_finish() yields the connect error and closes it.
 */
int probe_start(unsigned int cid, unsigned int port);
int probe_finish(int fd);

#endif

#ifndef KMORPH_CLIENT_H
#define KMORPH_CLIENT_H

#include <stddef.h>

/* Send one request line to kmorphd in instance @cid and read its one-line answer. */
int client_request(unsigned int cid, const char *request, unsigned int timeout_ms,
		   char *reply, size_t len);

#endif

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "kmorph/request.h"
#include "kmorph/vsock.h"
#include "client.h"

int client_request(unsigned int cid, const char *request, unsigned int timeout_ms,
		   char *reply, size_t len)
{
	struct sockaddr_vsock addr;
	struct timeval tv = { .tv_sec = timeout_ms / 1000,
			      .tv_usec = (timeout_ms % 1000) * 1000 };
	ssize_t n;
	int fd, ret = 0;

	fd = vsock_multikernel_socket(0);
	if (fd < 0)
		return fd;
	setsockopt(fd, AF_VSOCK, VSOCK_SO_CONNECT_TIMEOUT_OLD, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	memset(&addr, 0, sizeof(addr));
	addr.svm_family = AF_VSOCK;
	addr.svm_cid = cid;
	addr.svm_port = KMORPH_VSOCK_PORT;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    send(fd, request, strlen(request), MSG_NOSIGNAL) < 0) {
		ret = -errno;
		goto out;
	}
	n = recv(fd, reply, len - 1, 0);
	if (n <= 0) {
		ret = n ? -errno : -ECONNRESET;
		goto out;
	}
	reply[n] = '\0';
out:
	close(fd);
	return ret;
}

#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kmorph/probe.h"
#include "kmorph/vsock.h"

enum probe_result probe_classify(int err)
{
	switch (err) {
	case 0:
	case ECONNRESET:
		return PROBE_ALIVE;
	case ETIMEDOUT:
		return PROBE_SILENT;
	default:
		return PROBE_ERROR;
	}
}

const char *probe_result_name(enum probe_result r)
{
	switch (r) {
	case PROBE_ALIVE: return "alive";
	case PROBE_SILENT: return "silent";
	default: return "error";
	}
}

int probe_start(unsigned int cid, unsigned int port)
{
	struct sockaddr_vsock addr;
	int fd;

	fd = vsock_multikernel_socket(SOCK_NONBLOCK);
	if (fd < 0)
		return fd;

	memset(&addr, 0, sizeof(addr));
	addr.svm_family = AF_VSOCK;
	addr.svm_cid = cid;
	addr.svm_port = port;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 &&
	    errno != EINPROGRESS) {
		int err = errno;

		close(fd);
		return -err;
	}
	return fd;
}

int probe_finish(int fd)
{
	int err = 0;
	socklen_t len = sizeof(err);

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		err = errno;
	close(fd);
	return err;
}

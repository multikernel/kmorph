#include <errno.h>
#include <unistd.h>

#include "kmorph/vsock.h"

int vsock_multikernel_socket(int flags)
{
	int transport = VSOCK_TRANSPORT_MULTIKERNEL;
	int fd, err;

	fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC | flags, 0);
	if (fd < 0)
		return -errno;
	if (setsockopt(fd, AF_VSOCK, VSOCK_SO_TRANSPORT, &transport, sizeof(transport)) < 0) {
		err = errno;
		close(fd);
		return -err;
	}
	return fd;
}

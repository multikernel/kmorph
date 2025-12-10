#ifndef KMORPH_VSOCK_H
#define KMORPH_VSOCK_H

#include <stdint.h>
#include <sys/socket.h>

/*
 * From uapi/linux/vm_sockets.h, carried here so a libc without kernel
 * headers (musl) still builds a static binary.
 */

#ifndef AF_VSOCK
#define AF_VSOCK 40
/* A vsock stream socket bound to the multikernel transport, or -errno. */
int vsock_multikernel_socket(int flags);

#endif

#define VSOCK_CID_ANY ((unsigned int)-1)
#define VSOCK_SO_CONNECT_TIMEOUT_OLD 6

/* Multikernel additions: a socket opts into the cross-kernel transport. */
#define VSOCK_SO_TRANSPORT 9
#define VSOCK_TRANSPORT_MULTIKERNEL 1

struct sockaddr_vsock {
	sa_family_t svm_family;
	unsigned short svm_reserved1;
	unsigned int svm_port;
	unsigned int svm_cid;
	uint8_t svm_flags;
	unsigned char svm_zero[sizeof(struct sockaddr) - sizeof(sa_family_t) -
			       sizeof(unsigned short) - sizeof(unsigned int) -
			       sizeof(unsigned int) - sizeof(uint8_t)];
};

/* A vsock stream socket bound to the multikernel transport, or -errno. */
int vsock_multikernel_socket(int flags);

#endif

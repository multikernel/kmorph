#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/rtnl.h"

#define ALIGN4(n) (((n) + 3) & ~(size_t)3)
#define HDRLEN ALIGN4(sizeof(struct nlmsghdr))

void rtnl_req_init(struct rtnl_req *r, uint16_t type, uint16_t flags, const void *body,
		   size_t len)
{
	memset(r, 0, sizeof(*r));
	r->hdr.nlmsg_type = type;
	r->hdr.nlmsg_flags = NLM_F_REQUEST | flags;
	memcpy(r->buf + HDRLEN, body, len);
	r->hdr.nlmsg_len = HDRLEN + ALIGN4(len);
}

int rtnl_req_attr(struct rtnl_req *r, uint16_t type, const void *data, size_t len)
{
	struct rtattr *a = (struct rtattr *)(r->buf + r->hdr.nlmsg_len);
	size_t alen = ALIGN4(sizeof(*a)) + len;

	if (r->hdr.nlmsg_len + ALIGN4(alen) > sizeof(r->buf))
		return -E2BIG;
	a->rta_type = type;
	a->rta_len = alen;
	memcpy((unsigned char *)a + ALIGN4(sizeof(*a)), data, len);
	r->hdr.nlmsg_len += ALIGN4(alen);
	return 0;
}

void *rtnl_body(const struct nlmsghdr *h)
{
	return (unsigned char *)h + HDRLEN;
}

const struct rtattr *rtnl_attr(const struct nlmsghdr *h, size_t hdrlen, uint16_t type)
{
	const unsigned char *p = (const unsigned char *)h + HDRLEN + ALIGN4(hdrlen);
	const unsigned char *end = (const unsigned char *)h + h->nlmsg_len;

	while (p + sizeof(struct rtattr) <= end) {
		const struct rtattr *a = (const struct rtattr *)p;

		if (a->rta_len < sizeof(*a) || p + a->rta_len > end)
			break;
		if (a->rta_type == type)
			return a;
		p += ALIGN4(a->rta_len);
	}
	return NULL;
}

const void *rtnl_attr_data(const struct rtattr *a)
{
	return (const unsigned char *)a + ALIGN4(sizeof(*a));
}

size_t rtnl_attr_len(const struct rtattr *a)
{
	return a->rta_len - ALIGN4(sizeof(*a));
}

int rtnl_open(void)
{
	return rtnl_open_groups(0);
}

int rtnl_open_groups(uint32_t groups)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = groups };
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

	if (fd < 0)
		return -errno;
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		int err = errno;

		close(fd);
		return -err;
	}
	return fd;
}

static uint32_t next_seq(void)
{
	static uint32_t seq;

	return ++seq;
}

/* Reads replies to seq until the done or error message; cb sees the rest. */
static int receive(int fd, uint32_t seq, int (*cb)(const struct nlmsghdr *h, void *ctx),
		   void *ctx)
{
	static unsigned char buf[32768];

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		size_t off = 0;

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		while (off + sizeof(struct nlmsghdr) <= (size_t)n) {
			const struct nlmsghdr *h = (const struct nlmsghdr *)(buf + off);
			int ret;

			if (h->nlmsg_len < sizeof(*h) || off + h->nlmsg_len > (size_t)n)
				return -EBADMSG;
			off += ALIGN4(h->nlmsg_len);
			if (h->nlmsg_seq != seq)
				continue;
			if (h->nlmsg_type == NLMSG_DONE)
				return 0;
			if (h->nlmsg_type == NLMSG_ERROR)
				return ((const struct nlmsgerr *)rtnl_body(h))->error;
			if (cb) {
				ret = cb(h, ctx);
				if (ret)
					return ret;
			}
		}
	}
}

static int send_req(int fd, struct rtnl_req *r)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };

	r->hdr.nlmsg_seq = next_seq();
	if (sendto(fd, r->buf, r->hdr.nlmsg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		return -errno;
	return 0;
}

int rtnl_talk(int fd, struct rtnl_req *r)
{
	int ret;

	r->hdr.nlmsg_flags |= NLM_F_ACK;
	ret = send_req(fd, r);
	return ret ? ret : receive(fd, r->hdr.nlmsg_seq, NULL, NULL);
}

int rtnl_dump(int fd, uint16_t type, int (*cb)(const struct nlmsghdr *h, void *ctx), void *ctx)
{
	struct { unsigned char family; } g = { AF_UNSPEC };
	struct rtnl_req r;
	int ret;

	rtnl_req_init(&r, type, NLM_F_DUMP, &g, sizeof(g));
	ret = send_req(fd, &r);
	return ret ? ret : receive(fd, r.hdr.nlmsg_seq, cb, ctx);
}

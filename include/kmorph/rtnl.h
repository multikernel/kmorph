#ifndef KMORPH_RTNL_H
#define KMORPH_RTNL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

/* The static build's libc ships no kernel headers; the netlink ABI is stable. */
#if __has_include(<linux/rtnetlink.h>)
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#else
struct nlmsghdr {
	uint32_t nlmsg_len;
	uint16_t nlmsg_type;
	uint16_t nlmsg_flags;
	uint32_t nlmsg_seq;
	uint32_t nlmsg_pid;
};

struct nlmsgerr {
	int error;
	struct nlmsghdr msg;
};

struct sockaddr_nl {
	sa_family_t nl_family;
	unsigned short nl_pad;
	uint32_t nl_pid;
	uint32_t nl_groups;
};

struct rtattr {
	unsigned short rta_len;
	unsigned short rta_type;
};

struct ifinfomsg {
	unsigned char ifi_family;
	unsigned char __ifi_pad;
	unsigned short ifi_type;
	int ifi_index;
	unsigned int ifi_flags;
	unsigned int ifi_change;
};

struct ifaddrmsg {
	uint8_t ifa_family;
	uint8_t ifa_prefixlen;
	uint8_t ifa_flags;
	uint8_t ifa_scope;
	uint32_t ifa_index;
};

struct rtmsg {
	unsigned char rtm_family;
	unsigned char rtm_dst_len;
	unsigned char rtm_src_len;
	unsigned char rtm_tos;
	unsigned char rtm_table;
	unsigned char rtm_protocol;
	unsigned char rtm_scope;
	unsigned char rtm_type;
	unsigned int rtm_flags;
};

#define NETLINK_ROUTE	0
#define NLMSG_ERROR	2
#define NLMSG_DONE	3
#define NLM_F_REQUEST	0x01
#define NLM_F_MULTI	0x02
#define NLM_F_ACK	0x04
#define NLM_F_REPLACE	0x100
#define NLM_F_DUMP	0x300
#define NLM_F_CREATE	0x400

#define RTM_NEWLINK	16
#define RTM_NEWADDR	20
#define RTM_GETADDR	22
#define RTM_NEWROUTE	24
#define RTM_GETROUTE	26

#define IFLA_ADDRESS	1
#define IFLA_IFNAME	3
#define IFLA_MTU	4

#define IFA_ADDRESS	1
#define IFA_LOCAL	2
#define IFA_BROADCAST	4
#define IFA_FLAGS	8
#define IFA_F_TEMPORARY	0x01
#define IFA_F_NODAD	0x02

#define RTA_DST		1
#define RTA_OIF		4
#define RTA_GATEWAY	5
#define RTA_PRIORITY	6
#define RTA_MULTIPATH	9
#define RTA_TABLE	15

#define RT_TABLE_MAIN	254
#define RTN_UNICAST	1
#define RTPROT_KERNEL	2
#define RT_SCOPE_UNIVERSE 0
#define RT_SCOPE_LINK	253
#endif

#define RTNL_REQ_MAX 512

struct rtnl_req {
	union {
		struct nlmsghdr hdr;
		unsigned char buf[RTNL_REQ_MAX];
	};
};

/* Starts a request of the given type, with its fixed header copied in. */
void rtnl_req_init(struct rtnl_req *r, uint16_t type, uint16_t flags, const void *body,
		   size_t len);
int rtnl_req_attr(struct rtnl_req *r, uint16_t type, const void *data, size_t len);
/* The fixed header of a message, past its netlink header. */
void *rtnl_body(const struct nlmsghdr *h);
/* The attribute of the given type after a fixed header of hdrlen bytes, or NULL. */
const struct rtattr *rtnl_attr(const struct nlmsghdr *h, size_t hdrlen, uint16_t type);
const void *rtnl_attr_data(const struct rtattr *a);
size_t rtnl_attr_len(const struct rtattr *a);

int rtnl_open(void);
/* Sends a request and waits for its acknowledgement: 0 or the kernel's -errno. */
int rtnl_talk(int fd, struct rtnl_req *r);
/* Dumps every object of the type, calling cb on each message. */
int rtnl_dump(int fd, uint16_t type, int (*cb)(const struct nlmsghdr *h, void *ctx), void *ctx);

#endif

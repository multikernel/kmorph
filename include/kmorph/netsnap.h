#ifndef KMORPH_NETSNAP_H
#define KMORPH_NETSNAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "kmorph/parse.h"
#include "kmorph/rtnl.h"

/*
 * The network identity of the NICs the successor adopts: what arm sees
 * on the predecessor, so that the successor can put it back on the same
 * PCI functions the moment it owns them, without waiting for the disk's
 * network manager.
 */

#define NETSNAP_PATH		"/etc/kmorph/network"
#define NETSNAP_MAX_LINKS	8
#define NETSNAP_MAX_ADDRS	16
#define NETSNAP_MAX_ROUTES	32
#define NETSNAP_TEXT_MAX	16384

struct netsnap_addr {
	uint8_t family;
	uint8_t prefix;
	bool has_brd;
	uint8_t addr[16];
	uint8_t brd[16];
	/* Wall clock seconds, shared by both kernels of the machine; 0: forever */
	int64_t valid_until;
	int64_t preferred_until;
};

struct netsnap_route {
	uint8_t family;
	uint8_t dst_len;
	uint8_t protocol;
	uint8_t scope;
	bool has_gw;
	uint8_t dst[16];
	uint8_t gw[16];
	uint32_t metric;
};

struct netsnap_link {
	char pci[16];
	char name[16];
	uint8_t mac[6];
	unsigned int mtu;
	int ifindex;	/* on the predecessor, while collecting */
	struct netsnap_addr addrs[NETSNAP_MAX_ADDRS];
	size_t naddrs;
	struct netsnap_route routes[NETSNAP_MAX_ROUTES];
	size_t nroutes;
};

struct netsnap {
	struct netsnap_link links[NETSNAP_MAX_LINKS];
	size_t count;
};

/* One line per link, address and route; returns the length or -errno. */
int netsnap_format(const struct netsnap *s, char *buf, size_t len);
int netsnap_parse(const char *text, struct netsnap *s);

/*
 * The PCI network interfaces under class_net, but those whose function
 * is in skip, with what rtnetlink reports for them.
 */
int netsnap_collect(const char *class_net, const struct strlist *skip, struct netsnap *s);

struct netsnap_env {
	/* The interface on a PCI function once its driver created it; 0 or -errno. */
	int (*find)(const char *pci, char *name, size_t len, int *ifindex);
	int (*talk)(void *ctx, struct rtnl_req *r);
	time_t (*now)(void);
	void *ctx;
};

extern const struct netsnap_env netsnap_default_env;

/*
 * Puts each link's name, MAC, MTU, addresses and routes back, with what
 * is left of each address's lifetimes, marking
 * in restored (one per link, may be NULL) those that came up; returns
 * how many did.
 */
int netsnap_apply(const struct netsnap_env *env, const struct netsnap *s, bool *restored);

#endif

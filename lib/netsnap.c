#include <arpa/inet.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kmorph/devname.h"
#include "kmorph/file.h"
#include "kmorph/log.h"
#include "kmorph/netsnap.h"
#include "kmorph/pci.h"

#define FIND_TIMEOUT_MS	3000
#define FIND_POLL_MS	10
#define LIFE_FOREVER	0xffffffffu

static bool strlist_contains(const struct strlist *l, const char *s)
{
	size_t i;

	for (i = 0; l && i < l->count; i++)
		if (!strcmp(l->items[i], s))
			return true;
	return false;
}

static size_t addr_len(uint8_t family)
{
	return family == AF_INET6 ? 16 : 4;
}

static int put_ip(char *buf, size_t len, uint8_t family, const uint8_t *addr)
{
	return inet_ntop(family, addr, buf, len) ? 0 : -EINVAL;
}

static int get_ip(const char *text, uint8_t *family, uint8_t *addr)
{
	*family = strchr(text, ':') ? AF_INET6 : AF_INET;
	return inet_pton(*family, text, addr) == 1 ? 0 : -EINVAL;
}

/* An address with its prefix length, as in 10.0.0.1/24. */
static int get_prefix(char *text, uint8_t *family, uint8_t *addr, uint8_t *prefix)
{
	char *slash = strchr(text, '/'), *end;
	unsigned long n;

	if (!slash)
		return -EINVAL;
	*slash = '\0';
	n = strtoul(slash + 1, &end, 10);
	if (*end || get_ip(text, family, addr) || n > addr_len(*family) * 8)
		return -EINVAL;
	*prefix = n;
	return 0;
}

static const char *put_until(char *buf, size_t len, int64_t until)
{
	if (!until)
		return "forever";
	snprintf(buf, len, "%lld", (long long)until);
	return buf;
}

static int get_until(const char *text, int64_t *until)
{
	char *end;

	if (!strcmp(text, "forever")) {
		*until = 0;
		return 0;
	}
	*until = strtoll(text, &end, 10);
	return *end || *until <= 0 ? -EINVAL : 0;
}

static int append(char *buf, size_t len, size_t *off, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

static int append(char *buf, size_t len, size_t *off, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf + *off, len - *off, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= len - *off)
		return -ENOSPC;
	*off += n;
	return 0;
}

int netsnap_format(const struct netsnap *s, char *buf, size_t len)
{
	char a[INET6_ADDRSTRLEN], b[INET6_ADDRSTRLEN];
	size_t off = 0, i, j;
	int ret = 0;

	if (len)
		buf[0] = '\0';
	for (i = 0; i < s->count && !ret; i++) {
		const struct netsnap_link *l = &s->links[i];
		const uint8_t *m = l->mac;

		ret = append(buf, len, &off, "link %s %s %02x:%02x:%02x:%02x:%02x:%02x %u\n",
			     l->pci, l->name, m[0], m[1], m[2], m[3], m[4], m[5], l->mtu);
		for (j = 0; j < l->naddrs && !ret; j++) {
			const struct netsnap_addr *ad = &l->addrs[j];
			char v[24], p[24];

			ret = put_ip(a, sizeof(a), ad->family, ad->addr);
			if (!ret && ad->has_brd)
				ret = put_ip(b, sizeof(b), ad->family, ad->brd);
			if (!ret)
				ret = append(buf, len, &off, "addr %s %s/%u %s %s %s\n", l->pci, a,
					     ad->prefix, ad->has_brd ? b : "-",
					     put_until(v, sizeof(v), ad->valid_until),
					     put_until(p, sizeof(p), ad->preferred_until));
		}
		for (j = 0; j < l->nroutes && !ret; j++) {
			const struct netsnap_route *r = &l->routes[j];

			ret = put_ip(a, sizeof(a), r->family, r->dst);
			if (!ret && r->has_gw)
				ret = put_ip(b, sizeof(b), r->family, r->gw);
			if (!ret)
				ret = append(buf, len, &off, "route %s %s/%u %s %u %u %u\n", l->pci, a,
					     r->dst_len, r->has_gw ? b : "-", r->metric,
					     r->protocol, r->scope);
		}
	}
	return ret ? ret : (int)off;
}

static struct netsnap_link *link_by_pci(struct netsnap *s, const char *pci)
{
	size_t i;

	for (i = 0; i < s->count; i++)
		if (!strcmp(s->links[i].pci, pci))
			return &s->links[i];
	return NULL;
}

static int parse_link(struct netsnap *s, const char *pci, const char *rest)
{
	struct netsnap_link *l;
	unsigned int m[6], mtu;
	char name[IFNAMSIZ];
	int i;

	if (s->count == NETSNAP_MAX_LINKS || link_by_pci(s, pci) || strlen(pci) >= sizeof(l->pci))
		return -EINVAL;
	if (sscanf(rest, "%15s %x:%x:%x:%x:%x:%x %u", name, &m[0], &m[1], &m[2], &m[3],
		   &m[4], &m[5], &mtu) != 8)
		return -EINVAL;
	l = &s->links[s->count++];
	memset(l, 0, sizeof(*l));
	snprintf(l->pci, sizeof(l->pci), "%s", pci);
	snprintf(l->name, sizeof(l->name), "%s", name);
	for (i = 0; i < 6; i++)
		l->mac[i] = m[i];
	l->mtu = mtu;
	return 0;
}

static int parse_addr(struct netsnap_link *l, const char *rest)
{
	char addr[64], brd[64], valid[24], pref[24];
	struct netsnap_addr *a;
	uint8_t fam;

	if (l->naddrs == NETSNAP_MAX_ADDRS ||
	    sscanf(rest, "%63s %63s %23s %23s", addr, brd, valid, pref) != 4)
		return -EINVAL;
	a = &l->addrs[l->naddrs];
	memset(a, 0, sizeof(*a));
	if (get_prefix(addr, &a->family, a->addr, &a->prefix) ||
	    get_until(valid, &a->valid_until) || get_until(pref, &a->preferred_until))
		return -EINVAL;
	if (strcmp(brd, "-")) {
		if (get_ip(brd, &fam, a->brd) || fam != a->family)
			return -EINVAL;
		a->has_brd = true;
	}
	l->naddrs++;
	return 0;
}

static int parse_route(struct netsnap_link *l, const char *rest)
{
	unsigned int metric, protocol, scope;
	char dst[64], gw[64];
	struct netsnap_route *r;
	uint8_t fam;

	if (l->nroutes == NETSNAP_MAX_ROUTES ||
	    sscanf(rest, "%63s %63s %u %u %u", dst, gw, &metric, &protocol, &scope) != 5 ||
	    protocol > 255 || scope > 255)
		return -EINVAL;
	r = &l->routes[l->nroutes];
	memset(r, 0, sizeof(*r));
	if (get_prefix(dst, &r->family, r->dst, &r->dst_len))
		return -EINVAL;
	if (strcmp(gw, "-")) {
		if (get_ip(gw, &fam, r->gw) || fam != r->family)
			return -EINVAL;
		r->has_gw = true;
	}
	r->metric = metric;
	r->protocol = protocol;
	r->scope = scope;
	l->nroutes++;
	return 0;
}

int netsnap_parse(const char *text, struct netsnap *s)
{
	const char *p = text;

	memset(s, 0, sizeof(*s));
	while (*p) {
		char line[256], kind[8], pci[32];
		size_t n = strcspn(p, "\n");
		struct netsnap_link *l;
		int skip, ret;

		snprintf(line, sizeof(line), "%.*s", (int)n, p);
		p += n + (p[n] == '\n');
		if (!line[0])
			continue;
		if (sscanf(line, "%7s %31s %n", kind, pci, &skip) != 2)
			return -EINVAL;
		if (!strcmp(kind, "link")) {
			ret = parse_link(s, pci, line + skip);
		} else {
			l = link_by_pci(s, pci);
			if (!l)
				return -EINVAL;
			if (!strcmp(kind, "addr"))
				ret = parse_addr(l, line + skip);
			else if (!strcmp(kind, "route"))
				ret = parse_route(l, line + skip);
			else
				ret = -EINVAL;
		}
		if (ret)
			return ret;
	}
	return 0;
}

static struct netsnap_link *link_by_index(struct netsnap *s, int ifindex)
{
	size_t i;

	for (i = 0; i < s->count; i++)
		if (s->links[i].ifindex == ifindex)
			return &s->links[i];
	return NULL;
}

/* Addresses the kernel makes up by itself (link-local) or rotates (temporary) are left to it. */
static int collect_addr(const struct nlmsghdr *h, void *ctx)
{
	const struct ifaddrmsg *ifa = rtnl_body(h);
	struct netsnap_link *l = link_by_index(ctx, ifa->ifa_index);
	const struct rtattr *local, *brd, *flags, *ci;
	struct netsnap_addr *a;
	uint32_t f = ifa->ifa_flags;

	if (h->nlmsg_type != RTM_NEWADDR || !l || ifa->ifa_scope != RT_SCOPE_UNIVERSE ||
	    (ifa->ifa_family != AF_INET && ifa->ifa_family != AF_INET6))
		return 0;
	flags = rtnl_attr(h, sizeof(*ifa), IFA_FLAGS);
	if (flags && rtnl_attr_len(flags) == 4)
		memcpy(&f, rtnl_attr_data(flags), 4);
	/* The same bit means secondary for IPv4 */
	if (ifa->ifa_family == AF_INET6 && (f & IFA_F_TEMPORARY))
		return 0;
	local = rtnl_attr(h, sizeof(*ifa), ifa->ifa_family == AF_INET ? IFA_LOCAL : IFA_ADDRESS);
	if (!local || rtnl_attr_len(local) != addr_len(ifa->ifa_family))
		return 0;
	if (l->naddrs == NETSNAP_MAX_ADDRS)
		return -E2BIG;
	a = &l->addrs[l->naddrs++];
	memset(a, 0, sizeof(*a));
	a->family = ifa->ifa_family;
	a->prefix = ifa->ifa_prefixlen;
	memcpy(a->addr, rtnl_attr_data(local), rtnl_attr_len(local));
	brd = rtnl_attr(h, sizeof(*ifa), IFA_BROADCAST);
	if (brd && rtnl_attr_len(brd) == addr_len(a->family)) {
		memcpy(a->brd, rtnl_attr_data(brd), rtnl_attr_len(brd));
		a->has_brd = true;
	}
	ci = rtnl_attr(h, sizeof(*ifa), IFA_CACHEINFO);
	if (ci && rtnl_attr_len(ci) == sizeof(struct ifa_cacheinfo)) {
		struct ifa_cacheinfo c;
		time_t now = time(NULL);

		memcpy(&c, rtnl_attr_data(ci), sizeof(c));
		if (c.ifa_valid != LIFE_FOREVER)
			a->valid_until = now + c.ifa_valid;
		if (c.ifa_prefered != LIFE_FOREVER)
			a->preferred_until = now + c.ifa_prefered;
	}
	return 0;
}

/* The routes adding an address creates are the kernel's own; the rest are someone's config. */
static int collect_route(const struct nlmsghdr *h, void *ctx)
{
	const struct rtmsg *rt = rtnl_body(h);
	const struct rtattr *oif, *table, *dst, *gw, *prio;
	struct netsnap_route *r;
	struct netsnap_link *l;
	uint32_t tab = rt->rtm_table;
	int ifindex;

	if (h->nlmsg_type != RTM_NEWROUTE || rt->rtm_type != RTN_UNICAST ||
	    rt->rtm_protocol == RTPROT_KERNEL || rt->rtm_src_len ||
	    (rt->rtm_family != AF_INET && rt->rtm_family != AF_INET6) ||
	    rtnl_attr(h, sizeof(*rt), RTA_MULTIPATH))
		return 0;
	table = rtnl_attr(h, sizeof(*rt), RTA_TABLE);
	if (table && rtnl_attr_len(table) == 4)
		memcpy(&tab, rtnl_attr_data(table), 4);
	oif = rtnl_attr(h, sizeof(*rt), RTA_OIF);
	if (tab != RT_TABLE_MAIN || !oif || rtnl_attr_len(oif) != 4)
		return 0;
	memcpy(&ifindex, rtnl_attr_data(oif), 4);
	l = link_by_index(ctx, ifindex);
	if (!l)
		return 0;
	if (l->nroutes == NETSNAP_MAX_ROUTES)
		return -E2BIG;
	r = &l->routes[l->nroutes++];
	memset(r, 0, sizeof(*r));
	r->family = rt->rtm_family;
	r->dst_len = rt->rtm_dst_len;
	r->protocol = rt->rtm_protocol;
	r->scope = rt->rtm_scope;
	dst = rtnl_attr(h, sizeof(*rt), RTA_DST);
	if (dst && rtnl_attr_len(dst) == addr_len(r->family))
		memcpy(r->dst, rtnl_attr_data(dst), rtnl_attr_len(dst));
	gw = rtnl_attr(h, sizeof(*rt), RTA_GATEWAY);
	if (gw && rtnl_attr_len(gw) == addr_len(r->family)) {
		memcpy(r->gw, rtnl_attr_data(gw), rtnl_attr_len(gw));
		r->has_gw = true;
	}
	prio = rtnl_attr(h, sizeof(*rt), RTA_PRIORITY);
	if (prio && rtnl_attr_len(prio) == 4)
		memcpy(&r->metric, rtnl_attr_data(prio), 4);
	return 0;
}

/* A virtio NIC's device is a virtio one; the PCI function sits above it. */
static int read_link(const char *class_net, const char *name, struct netsnap_link *l)
{
	char path[PATH_MAX], class_root[PATH_MAX], *s = NULL, *slash;
	unsigned int m[6];
	int i, ret;

	snprintf(class_root, sizeof(class_root), "%s", class_net);
	slash = strrchr(class_root, '/');
	if (!slash || strlen(name) >= sizeof(l->name))
		return -ENODEV;
	*slash = '\0';
	if (devname_resolve(class_root, DEVNAME_USB_ROOT, name, l->pci, sizeof(l->pci)))
		return -ENODEV;
	memcpy(l->name, name, strlen(name) + 1);

	snprintf(path, sizeof(path), "%s/%s/address", class_net, name);
	ret = file_read_string(path, &s);
	if (ret)
		return ret;
	ret = sscanf(s, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 ?
	      0 : -EINVAL;
	free(s);
	for (i = 0; i < 6; i++)
		l->mac[i] = m[i];
	snprintf(path, sizeof(path), "%s/%s/mtu", class_net, name);
	if (!ret && !file_read_string(path, &s)) {
		l->mtu = strtoul(s, NULL, 10);
		free(s);
	}
	snprintf(path, sizeof(path), "%s/%s/ifindex", class_net, name);
	if (!ret && !file_read_string(path, &s)) {
		l->ifindex = strtol(s, NULL, 10);
		free(s);
	}
	return ret;
}

int netsnap_collect(const char *class_net, const struct strlist *skip, struct netsnap *s)
{
	struct dirent *e;
	DIR *dir;
	int fd, ret = 0;

	memset(s, 0, sizeof(*s));
	dir = opendir(class_net);
	if (!dir)
		return -errno;
	while ((e = readdir(dir)) && s->count < NETSNAP_MAX_LINKS) {
		struct netsnap_link *l = &s->links[s->count];

		if (e->d_name[0] == '.')
			continue;
		memset(l, 0, sizeof(*l));
		if (read_link(class_net, e->d_name, l) || strlist_contains(skip, l->pci))
			continue;
		s->count++;
	}
	closedir(dir);
	if (!s->count)
		return 0;

	fd = rtnl_open();
	if (fd < 0)
		return fd;
	ret = rtnl_dump(fd, RTM_GETADDR, collect_addr, s);
	if (!ret)
		ret = rtnl_dump(fd, RTM_GETROUTE, collect_route, s);
	close(fd);
	return ret;
}

int netsnap_text(const struct strlist *skip, char *buf, size_t len)
{
	static struct netsnap snap;
	int ret;

	ret = netsnap_collect("/sys/class/net", skip, &snap);
	if (!ret)
		ret = netsnap_format(&snap, buf, len);
	return ret < 0 ? ret : (int)snap.count;
}

static void sleep_ms(unsigned int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

static int netdev_on(const char *pci, char *name, size_t len)
{
	char found[16];
	struct dirent *e;
	DIR *dir = opendir(DEVNAME_CLASS_ROOT "/net");
	int ret = -ENOENT;

	while (dir && (e = readdir(dir))) {
		if (e->d_name[0] == '.' ||
		    devname_resolve(DEVNAME_CLASS_ROOT, DEVNAME_USB_ROOT, e->d_name, found,
				    sizeof(found)) || strcmp(found, pci))
			continue;
		snprintf(name, len, "%s", e->d_name);
		ret = 0;
		break;
	}
	if (dir)
		closedir(dir);
	return ret;
}

/*
 * The driver binds as the device is adopted, but may create its interface a little later.
 * A function that is not on the bus was never adopted, and no interface will come for it.
 */
static int find_netdev(const char *pci, char *name, size_t len, int *ifindex)
{
	char dev[PATH_MAX];
	unsigned int waited;

	snprintf(dev, sizeof(dev), PCI_SYSFS_ROOT "/%s", pci);
	if (access(dev, F_OK))
		return -ENODEV;
	for (waited = 0; waited <= FIND_TIMEOUT_MS; waited += FIND_POLL_MS) {
		if (!netdev_on(pci, name, len)) {
			*ifindex = if_nametoindex(name);
			return *ifindex ? 0 : -errno;
		}
		sleep_ms(FIND_POLL_MS);
	}
	return -ETIMEDOUT;
}

static int default_talk(void *ctx, struct rtnl_req *r)
{
	static int fd = -1;

	(void)ctx;
	if (fd < 0)
		fd = rtnl_open();
	return fd < 0 ? fd : rtnl_talk(fd, r);
}

static time_t wall_clock(void)
{
	return time(NULL);
}

const struct netsnap_env netsnap_default_env = { find_netdev, default_talk, wall_clock, NULL };

/* Renaming and setting the MAC need the link down, as it still is right after the driver binds. */
static int restore_link(const struct netsnap_env *env, const struct netsnap_link *l,
			int ifindex, const char *cur)
{
	struct ifinfomsg ifi = { .ifi_family = AF_UNSPEC, .ifi_index = ifindex };
	struct rtnl_req r;
	int ret;

	rtnl_req_init(&r, RTM_NEWLINK, 0, &ifi, sizeof(ifi));
	rtnl_req_attr(&r, IFLA_ADDRESS, l->mac, sizeof(l->mac));
	if (l->mtu)
		rtnl_req_attr(&r, IFLA_MTU, &l->mtu, sizeof(l->mtu));
	if (strcmp(cur, l->name))
		rtnl_req_attr(&r, IFLA_IFNAME, l->name, strlen(l->name) + 1);
	ret = env->talk(env->ctx, &r);
	if (ret)
		log_warn("network: cannot restore %s as %s: %s", cur, l->name, strerror(-ret));

	ifi.ifi_flags = IFF_UP;
	ifi.ifi_change = IFF_UP;
	rtnl_req_init(&r, RTM_NEWLINK, 0, &ifi, sizeof(ifi));
	return env->talk(env->ctx, &r);
}

static uint32_t life_left(int64_t until, time_t now)
{
	if (!until)
		return LIFE_FOREVER;
	return until > now ? (uint32_t)(until - now) : 0;
}

/* A lease that ran out while the machine changed kernels is not the machine's any more. */
static int restore_addr(const struct netsnap_env *env, const struct netsnap_addr *a, int ifindex)
{
	time_t now = env->now();
	struct ifa_cacheinfo ci = {
		.ifa_valid = life_left(a->valid_until, now),
		.ifa_prefered = life_left(a->preferred_until, now),
	};
	struct ifaddrmsg ifa = {
		.ifa_family = a->family,
		.ifa_prefixlen = a->prefix,
		.ifa_index = ifindex,
	};
	/* The address was the machine's a moment ago; nobody else holds it. */
	uint32_t flags = IFA_F_NODAD;
	struct rtnl_req r;

	if (!ci.ifa_valid)
		return -ETIME;
	if (ci.ifa_prefered > ci.ifa_valid)
		ci.ifa_prefered = ci.ifa_valid;
	ifa.ifa_flags = flags;
	rtnl_req_init(&r, RTM_NEWADDR, NLM_F_CREATE | NLM_F_REPLACE, &ifa, sizeof(ifa));
	rtnl_req_attr(&r, IFA_LOCAL, a->addr, addr_len(a->family));
	rtnl_req_attr(&r, IFA_ADDRESS, a->addr, addr_len(a->family));
	if (a->has_brd)
		rtnl_req_attr(&r, IFA_BROADCAST, a->brd, addr_len(a->family));
	rtnl_req_attr(&r, IFA_FLAGS, &flags, sizeof(flags));
	rtnl_req_attr(&r, IFA_CACHEINFO, &ci, sizeof(ci));
	return env->talk(env->ctx, &r);
}

static int restore_route(const struct netsnap_env *env, const struct netsnap_route *rt,
			 int ifindex)
{
	struct rtmsg m = {
		.rtm_family = rt->family,
		.rtm_dst_len = rt->dst_len,
		.rtm_table = RT_TABLE_MAIN,
		.rtm_protocol = rt->protocol,
		.rtm_scope = rt->scope,
		.rtm_type = RTN_UNICAST,
	};
	uint32_t oif = ifindex;
	struct rtnl_req r;

	rtnl_req_init(&r, RTM_NEWROUTE, NLM_F_CREATE | NLM_F_REPLACE, &m, sizeof(m));
	if (rt->dst_len)
		rtnl_req_attr(&r, RTA_DST, rt->dst, addr_len(rt->family));
	if (rt->has_gw)
		rtnl_req_attr(&r, RTA_GATEWAY, rt->gw, addr_len(rt->family));
	rtnl_req_attr(&r, RTA_OIF, &oif, sizeof(oif));
	if (rt->metric)
		rtnl_req_attr(&r, RTA_PRIORITY, &rt->metric, sizeof(rt->metric));
	return env->talk(env->ctx, &r);
}

static void restore_routes(const struct netsnap_env *env, const struct netsnap_link *l,
			   int ifindex)
{
	size_t i;
	int pass;

	/* A gateway is reachable only once the routes to its subnet are in. */
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < l->nroutes; i++) {
			const struct netsnap_route *rt = &l->routes[i];
			int ret;

			if (rt->has_gw != (pass == 1))
				continue;
			ret = restore_route(env, rt, ifindex);
			if (ret)
				log_warn("network: %s: route %zu not restored: %s", l->name, i,
					 strerror(-ret));
		}
	}
}

int netsnap_apply(const struct netsnap_env *env, const struct netsnap *s, bool *restored)
{
	char done[768] = "";
	size_t i, j, len = 0;
	int up = 0;

	for (i = 0; i < s->count; i++) {
		const struct netsnap_link *l = &s->links[i];
		char cur[IFNAMSIZ];
		int ifindex, ret;

		if (restored)
			restored[i] = false;
		ret = env->find(l->pci, cur, sizeof(cur), &ifindex);
		if (ret) {
			log_warn("network: no interface on %s for %s: %s", l->pci, l->name,
				 strerror(-ret));
			continue;
		}
		ret = restore_link(env, l, ifindex, cur);
		if (ret) {
			log_warn("network: cannot bring %s up: %s", l->name, strerror(-ret));
			continue;
		}
		for (j = 0; j < l->naddrs; j++) {
			ret = restore_addr(env, &l->addrs[j], ifindex);
			if (ret == -ETIME)
				log_info("network: %s: the lease of address %zu ran out", l->name, j);
			else if (ret)
				log_warn("network: %s: address %zu not restored: %s", l->name, j,
					 strerror(-ret));
		}
		restore_routes(env, l, ifindex);
		if (len < sizeof(done))
			len += snprintf(done + len, sizeof(done) - len, "%s%s on %s (%zu addresses, %zu routes)",
					up ? ", " : "", l->name, l->pci, l->naddrs, l->nroutes);
		if (restored)
			restored[i] = true;
		up++;
	}
	/* One line however many NICs: takeover lines share the /dev/kmsg ratelimit */
	if (up)
		log_info("network: restored %s", done);
	return up;
}

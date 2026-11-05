#include <arpa/inet.h>
#include <net/if.h>

#include "test.h"
#include "kmorph/netsnap.h"

static const char snapshot[] =
	"link 0000:01:00.0 enp1s0 52:54:00:12:34:56 1500\n"
	"addr 0000:01:00.0 192.168.122.59/24 192.168.122.255\n"
	"addr 0000:01:00.0 fd00::59/64 -\n"
	"route 0000:01:00.0 0.0.0.0/0 192.168.122.1 100 16 0\n"
	"route 0000:01:00.0 10.1.0.0/16 - 0 4 253\n"
	"link 0000:02:00.0 enp2s0 52:54:00:ab:cd:ef 9000\n";

static void a_snapshot_survives_formatting(void)
{
	static struct netsnap s;
	char buf[1024];

	CHECK_EQ(netsnap_parse(snapshot, &s), 0);
	CHECK_EQ(s.count, 2);
	CHECK_STREQ(s.links[0].name, "enp1s0");
	CHECK_EQ(s.links[0].mac[5], 0x56);
	CHECK_EQ(s.links[0].naddrs, 2);
	CHECK_EQ(s.links[0].addrs[1].family, AF_INET6);
	CHECK(!s.links[0].addrs[1].has_brd);
	CHECK_EQ(s.links[0].nroutes, 2);
	CHECK(s.links[0].routes[0].has_gw);
	CHECK_EQ(s.links[0].routes[0].metric, 100);
	CHECK_EQ(s.links[0].routes[1].scope, 253);
	CHECK_EQ(s.links[1].mtu, 9000);
	CHECK_EQ(netsnap_format(&s, buf, sizeof(buf)), (int)strlen(snapshot));
	CHECK_STREQ(buf, snapshot);
}

static void malformed_lines_are_refused(void)
{
	static struct netsnap s;

	CHECK(netsnap_parse("addr 0000:01:00.0 10.0.0.1/24 -\n", &s) < 0);
	CHECK(netsnap_parse("link 0000:01:00.0 eth0 52:54:00:12:34 1500\n", &s) < 0);
	CHECK(netsnap_parse("link 0000:01:00.0 eth0 52:54:00:12:34:56 1500\n"
			    "addr 0000:01:00.0 10.0.0.1/33 -\n", &s) < 0);
	CHECK(netsnap_parse("link 0000:01:00.0 eth0 52:54:00:12:34:56 1500\n"
			    "route 0000:01:00.0 0.0.0.0/0 fd00::1 0 4 0\n", &s) < 0);
	CHECK(netsnap_parse("bogus 0000:01:00.0\n", &s) < 0);
}

static char trace[2048];
static int missing_pci;

static int fake_find(const char *pci, char *name, size_t len, int *ifindex)
{
	if (missing_pci && !strcmp(pci, "0000:02:00.0"))
		return -ETIMEDOUT;
	snprintf(name, len, "eth%c", pci[6]);
	*ifindex = 7;
	return 0;
}

static int fake_talk(void *ctx, struct rtnl_req *r)
{
	char line[128];
	const struct rtattr *a;

	(void)ctx;
	switch (r->hdr.nlmsg_type) {
	case RTM_NEWLINK: {
		const struct ifinfomsg *ifi = rtnl_body(&r->hdr);

		a = rtnl_attr(&r->hdr, sizeof(*ifi), IFLA_IFNAME);
		snprintf(line, sizeof(line), "link %d%s%s%s;", ifi->ifi_index,
			 ifi->ifi_flags & IFF_UP ? " up" : "",
			 a ? " name " : "", a ? (const char *)rtnl_attr_data(a) : "");
		break;
	}
	case RTM_NEWADDR: {
		const struct ifaddrmsg *ifa = rtnl_body(&r->hdr);
		char ip[INET6_ADDRSTRLEN];

		a = rtnl_attr(&r->hdr, sizeof(*ifa), IFA_LOCAL);
		inet_ntop(ifa->ifa_family, rtnl_attr_data(a), ip, sizeof(ip));
		snprintf(line, sizeof(line), "addr %s/%u%s;", ip, ifa->ifa_prefixlen,
			 ifa->ifa_flags & IFA_F_NODAD ? " nodad" : "");
		break;
	}
	case RTM_NEWROUTE: {
		const struct rtmsg *rt = rtnl_body(&r->hdr);

		a = rtnl_attr(&r->hdr, sizeof(*rt), RTA_GATEWAY);
		snprintf(line, sizeof(line), "route /%u%s;", rt->rtm_dst_len, a ? " via" : "");
		break;
	}
	default:
		snprintf(line, sizeof(line), "?;");
	}
	strncat(trace, line, sizeof(trace) - strlen(trace) - 1);
	return 0;
}

static const struct netsnap_env env = { fake_find, fake_talk, NULL };

static void each_link_is_named_raised_addressed_and_routed(void)
{
	static struct netsnap s;

	CHECK_EQ(netsnap_parse(snapshot, &s), 0);
	trace[0] = '\0';
	missing_pci = 0;
	CHECK_EQ(netsnap_apply(&env, &s, NULL), 2);
	CHECK_STREQ(trace,
		    "link 7 name enp1s0;link 7 up;"
		    "addr 192.168.122.59/24 nodad;addr fd00::59/64 nodad;"
		    "route /16;route /0 via;"
		    "link 7 name enp2s0;link 7 up;");
}

static void a_nic_that_never_appears_is_skipped(void)
{
	static struct netsnap s;
	bool restored[NETSNAP_MAX_LINKS];

	CHECK_EQ(netsnap_parse(snapshot, &s), 0);
	trace[0] = '\0';
	missing_pci = 1;
	CHECK_EQ(netsnap_apply(&env, &s, restored), 1);
	CHECK(!strstr(trace, "enp2s0"));
	CHECK(restored[0]);
	CHECK(!restored[1]);
}

TEST_MAIN({
	RUN(a_snapshot_survives_formatting);
	RUN(malformed_lines_are_refused);
	RUN(each_link_is_named_raised_addressed_and_routed);
	RUN(a_nic_that_never_appears_is_skipped);
})

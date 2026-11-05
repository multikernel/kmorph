#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "kmorph/log.h"
#include "kmorph/netsnap.h"
#include "kmorph/request.h"
#include "client.h"
#include "watch.h"

#define REQUEST_TIMEOUT_MS	2000
#define RETRY_MS		1000
#define SETTLE_MS		100
#define SETTLE_MAX_MS		1000
#define GROUPS (RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE | \
		RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE)

static int send_text(unsigned int id, const char *text)
{
	static char req[64 + NETSNAP_TEXT_MAX];
	char reply[128], reason[96];
	size_t len = strlen(text);
	int n, ret;

	n = request_format_network(len, req, sizeof(req));
	if (n < 0 || n + len >= sizeof(req))
		return -E2BIG;
	memcpy(req + n, text, len + 1);
	ret = client_request(id, req, REQUEST_TIMEOUT_MS, reply, sizeof(reply));
	if (ret)
		return ret;
	ret = request_parse_network_reply(reply, reason, sizeof(reason));
	if (ret == -EBUSY)
		log_warn("network: the successor refused the identity: %s", reason);
	return ret;
}

int watch_push(unsigned int id, const struct strlist *skip)
{
	static char text[NETSNAP_TEXT_MAX];
	int ret = netsnap_text(skip, text, sizeof(text));

	return ret < 0 ? ret : send_text(id, text);
}

static void drain(int fd)
{
	char buf[8192];

	while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0)
		;
}

/* An address change comes as a burst of messages; one snapshot covers them all. */
static void wait_change(int fd, int timeout_ms)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int settled = 0;

	if (poll(&pfd, 1, timeout_ms) <= 0)
		return;
	drain(fd);
	while (settled < SETTLE_MAX_MS && poll(&pfd, 1, SETTLE_MS) > 0) {
		drain(fd);
		settled += SETTLE_MS;
	}
}

int watch_run(const struct kmorph_config *cfg, const struct mkfs *fs)
{
	static char sent[NETSNAP_TEXT_MAX], now[NETSNAP_TEXT_MAX];
	bool synced = false;
	int fd, ret, err, last_err = 0;
	uint32_t id;

	if (mkfs_instance_id(fs, cfg->name, &id)) {
		log_err("watch: %s is not armed", cfg->name);
		return -ENOENT;
	}
	/* Subscribed before the first snapshot, so that no change slips in between */
	fd = rtnl_open_groups(GROUPS);
	if (fd < 0) {
		log_err("watch: cannot listen to rtnetlink: %s", strerror(-fd));
		return fd;
	}
	log_info("watch: handing network changes to %s", cfg->name);
	for (;;) {
		ret = netsnap_text(&cfg->devices, now, sizeof(now));
		if (ret < 0) {
			log_warn("watch: cannot read the network identity: %s", strerror(-ret));
		} else if (!synced || strcmp(now, sent)) {
			err = send_text(id, now);
			synced = !err;
			if (synced) {
				memcpy(sent, now, sizeof(sent));
				log_info("watch: network identity of %d interfaces sent", ret);
			} else if (err != last_err) {
				/* kmorphd may still be starting; this repeats every second until it answers */
				log_warn("watch: cannot send the network identity: %s", strerror(-err));
			}
			last_err = err;
		}
		wait_change(fd, synced && ret >= 0 ? -1 : RETRY_MS);
	}
}

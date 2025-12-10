/*
 * kmorph: the operator's command on the predecessor. Arms a successor,
 * disarms it, or reports its state.
 */
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "kmorph/config.h"
#include "kmorph/log.h"
#include "kmorph/mkfs.h"
#include "kmorph/request.h"
#include "kmorph/vsock.h"
#include "arm.h"

#define STATUS_TIMEOUT_MS 2000

static int ask_daemon(unsigned int cid, struct status_reply *reply)
{
	struct sockaddr_vsock addr;
	struct timeval tv = { .tv_sec = STATUS_TIMEOUT_MS / 1000,
			      .tv_usec = (STATUS_TIMEOUT_MS % 1000) * 1000 };
	char line[128];
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
	    send(fd, "status\n", 7, MSG_NOSIGNAL) < 0) {
		ret = -errno;
		goto out;
	}
	n = recv(fd, line, sizeof(line) - 1, 0);
	if (n <= 0) {
		ret = n ? -errno : -ECONNRESET;
		goto out;
	}
	line[n] = '\0';
	ret = request_parse_status(line, reply);
out:
	close(fd);
	return ret;
}

static int status_run(const struct kmorph_config *cfg, const struct mkfs *fs)
{
	struct status_reply reply;
	char status[32];
	uint32_t id;
	int ret;

	ret = mkfs_instance_id(fs, cfg->name, &id);
	if (ret) {
		printf("successor: not armed\n");
		return 1;
	}
	if (mkfs_instance_status(fs, cfg->name, status, sizeof(status)))
		snprintf(status, sizeof(status), "unknown");
	printf("successor: %s (instance %s, id %u)\n", status, cfg->name, id);

	ret = ask_daemon(id, &reply);
	if (ret) {
		printf("kmorphd: unreachable (%s)\n", strerror(-ret));
		return 1;
	}
	printf("kmorphd: %s predecessor=%s last_probe=%llums error=%d\n",
	       reply.state, reply.predecessor,
	       (unsigned long long)reply.last_probe_ms, reply.last_error);
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "usage: kmorph <arm|disarm|status> [--config PATH]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ NULL, 0, NULL, 0 },
	};
	struct kmorph_config cfg;
	struct mkfs fs = { MKFS_ROOT };
	const char *config_path = KMORPH_CONFIG_PATH, *verb;
	char err[128];
	int c, ret;

	while ((c = getopt_long(argc, argv, "c:", opts, NULL)) != -1) {
		switch (c) {
		case 'c': config_path = optarg; break;
		default: usage();
		}
	}
	if (optind + 1 != argc)
		usage();
	verb = argv[optind];

	log_init("kmorph", STDERR_FILENO, -1);
	ret = config_load(config_path, &cfg, err, sizeof(err));
	if (ret) {
		log_err("config: %s", err);
		return 1;
	}
	ret = mkfs_mount(&fs);
	if (ret) {
		log_err("cannot mount %s: %s", fs.root, strerror(-ret));
		return 1;
	}

	if (!strcmp(verb, "arm"))
		ret = arm_run(&cfg, &fs, &arm_default_hooks);
	else if (!strcmp(verb, "disarm"))
		ret = disarm_run(&cfg, &fs, &arm_default_hooks);
	else if (!strcmp(verb, "status"))
		ret = status_run(&cfg, &fs);
	else if (!strcmp(verb, "upgrade")) {
		log_err("planned takeover is not implemented yet");
		ret = -ENOSYS;
	} else
		usage();

	config_free(&cfg);
	return ret ? 1 : 0;
}

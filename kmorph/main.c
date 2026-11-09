/*
 * kmorph: the operator's command on the predecessor. Arms a successor,
 * disarms it, reports its state, or hands the machine over to it.
 */
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/config.h"
#include "kmorph/devtree.h"
#include "kmorph/log.h"
#include "kmorph/mkfs.h"
#include "kmorph/request.h"
#include "arm.h"
#include "client.h"
#include "upgrade.h"
#include "watch.h"

#define STATUS_TIMEOUT_MS 2000

static int ask_daemon(unsigned int cid, struct status_reply *reply)
{
	char line[128];
	int ret = client_request(cid, "status\n", STATUS_TIMEOUT_MS, line, sizeof(line));

	return ret ? ret : request_parse_status(line, reply);
}

static int status_run(const char *name, const struct mkfs *fs)
{
	struct status_reply reply;
	char status[32];
	uint32_t id;
	int ret;

	ret = mkfs_instance_id(fs, name, &id);
	if (ret) {
		printf("successor: not armed\n");
		return 1;
	}
	if (mkfs_instance_status(fs, name, status, sizeof(status)))
		snprintf(status, sizeof(status), "unknown");
	printf("successor: %s (instance %s, id %u)\n", status, name, id);

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
	fprintf(stderr, "usage: kmorph <arm|disarm|status|upgrade|watch> [--config PATH]\n");
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
	char err[128], name[32];
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
	devtree_successor_name(DEVTREE_ROOT, name, sizeof(name));

	if (!strcmp(verb, "arm"))
		ret = arm_run(&cfg, name, &fs, &arm_default_hooks);
	else if (!strcmp(verb, "disarm"))
		ret = disarm_run(name, &fs, &arm_default_hooks);
	else if (!strcmp(verb, "status"))
		ret = status_run(name, &fs);
	else if (!strcmp(verb, "upgrade"))
		ret = upgrade_run(&cfg, name, &fs);
	else if (!strcmp(verb, "watch"))
		ret = watch_run(&cfg, name, &fs);
	else
		usage();

	config_free(&cfg);
	return ret ? 1 : 0;
}

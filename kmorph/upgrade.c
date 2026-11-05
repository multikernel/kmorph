#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/log.h"
#include "kmorph/request.h"
#include "client.h"
#include "upgrade.h"

#define REQUEST_TIMEOUT_MS	2000
#define FENCE_WAIT_S		10

int upgrade_run(const struct kmorph_config *cfg, const struct mkfs *fs)
{
	char other[64], reply[128], reason[96];
	uint32_t id;
	int ret;

	if (mkfs_instance_id(fs, cfg->name, &id)) {
		log_err("upgrade: %s is not armed", cfg->name);
		return -ENOENT;
	}
	ret = mkfs_other_running(fs, cfg->name, other, sizeof(other));
	if (ret > 0) {
		log_err("upgrade: %s runs under this kernel too and would lose its host", other);
		return -EBUSY;
	}
	if (ret) {
		log_err("upgrade: cannot list instances: %s", strerror(-ret));
		return ret;
	}

	/* What is still only in memory here would be lost with this kernel */
	sync();
	ret = client_request(id, "handover\n", REQUEST_TIMEOUT_MS, reply, sizeof(reply));
	if (ret) {
		log_err("upgrade: no answer from kmorphd in %s: %s", cfg->name, strerror(-ret));
		return ret;
	}
	ret = request_parse_handover(reply, reason, sizeof(reason));
	if (ret == -EBUSY) {
		log_err("upgrade: successor refused: %s", reason);
		return ret;
	}
	if (ret) {
		log_err("upgrade: kmorphd answered '%.*s'", (int)strcspn(reply, "\n"), reply);
		return ret;
	}
	log_info("upgrade: successor accepted; it fences this kernel now");
	sleep(FENCE_WAIT_S);
	log_err("upgrade: not fenced within %d s; this kernel stays host", FENCE_WAIT_S);
	return -ETIMEDOUT;
}

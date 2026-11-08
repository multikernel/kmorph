#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/log.h"
#include "kmorph/request.h"
#include "client.h"
#include "freeze.h"
#include "upgrade.h"
#include "watch.h"

#define REQUEST_TIMEOUT_MS	2000
#define FENCE_WAIT_S		10

/* Filesystems frozen now need no journal replay when the successor mounts them. */
static int freeze_filesystems(struct freeze_set *set)
{
	char *mountinfo;
	int ret;

	ret = file_read_string("/proc/self/mountinfo", &mountinfo);
	if (ret)
		return ret;
	ret = freeze_collect(mountinfo, set);
	free(mountinfo);
	if (!ret)
		ret = freeze_all(&freeze_default_ops, set);
	if (ret)
		freeze_set_free(set);
	return ret;
}

int upgrade_run(const struct kmorph_config *cfg, const char *name, const struct mkfs *fs)
{
	char other[64], reply[128], reason[96];
	struct freeze_set frozen;
	uint32_t id;
	int ret;

	if (mkfs_instance_id(fs, name, &id)) {
		log_err("upgrade: %s is not armed", name);
		return -ENOENT;
	}
	ret = mkfs_other_running(fs, name, other, sizeof(other));
	if (ret > 0) {
		log_err("upgrade: %s runs under this kernel too and would lose its host", other);
		return -EBUSY;
	}
	if (ret) {
		log_err("upgrade: cannot list instances: %s", strerror(-ret));
		return ret;
	}

	ret = watch_push(id, &cfg->devices);
	if (ret)
		log_warn("upgrade: the successor keeps the last network identity it got: %s",
			 strerror(-ret));
	/* What is still only in memory here would be lost with this kernel */
	sync();
	ret = freeze_filesystems(&frozen);
	if (ret) {
		log_err("upgrade: cannot freeze the filesystems: %s", strerror(-ret));
		return ret;
	}
	log_info("upgrade: froze %zu filesystems", frozen.count);
	ret = client_request(id, "handover\n", REQUEST_TIMEOUT_MS, reply, sizeof(reply));
	if (ret) {
		log_err("upgrade: no answer from kmorphd in %s: %s", name, strerror(-ret));
		goto thaw;
	}
	ret = request_parse_handover(reply, reason, sizeof(reason));
	if (ret == -EBUSY) {
		log_err("upgrade: successor refused: %s", reason);
		goto thaw;
	}
	if (ret) {
		log_err("upgrade: kmorphd answered '%.*s'", (int)strcspn(reply, "\n"), reply);
		goto thaw;
	}
	log_info("upgrade: successor accepted; it fences this kernel now");
	sleep(FENCE_WAIT_S);
	log_err("upgrade: not fenced within %d s; this kernel stays host", FENCE_WAIT_S);
	ret = -ETIMEDOUT;
thaw:
	thaw_all(&freeze_default_ops, &frozen);
	freeze_set_free(&frozen);
	return ret;
}

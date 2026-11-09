#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "kmorph/file.h"
#include "kmorph/log.h"
#include "image.h"

/* Its exit status, or -errno when it could not run. */
static int mkimage(const char *mode)
{
	int status;
	pid_t pid = fork();

	if (pid < 0)
		return -errno;
	if (!pid) {
		execl(KMORPH_MKIMAGE, KMORPH_MKIMAGE, mode, (char *)NULL);
		log_err("cannot run %s: %s", KMORPH_MKIMAGE, strerror(errno));
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int image_load(void **image, size_t *len)
{
	int ret = mkimage("--if-stale");

	if (ret) {
		if (access(KMORPH_INITRD_PATH, F_OK)) {
			log_err("no successor image at %s, and kmorph-mkimage could not build it",
				KMORPH_INITRD_PATH);
			return ret < 0 ? ret : -EIO;
		}
		log_warn("kmorph-mkimage failed; arming with the image built before");
	}
	ret = file_read(KMORPH_INITRD_PATH, image, len);
	if (ret)
		log_err("cannot read successor image %s: %s", KMORPH_INITRD_PATH, strerror(-ret));
	return ret;
}

int image_run(const struct kmorph_config *cfg)
{
	int ret;

	if (cfg->initrd) {
		log_info("initrd = %s is an image of its own; kmorph builds nothing", cfg->initrd);
		return 0;
	}
	ret = mkimage(NULL);
	return ret <= 0 ? ret : -EIO;
}

/* kmorph-mkimage alone knows what an image was built from. */
static void describe_built(FILE *out)
{
	char built[32], reason[256] = "";
	struct stat st;
	FILE *check;
	int status = -1;

	if (stat(KMORPH_INITRD_PATH, &st) < 0) {
		fprintf(out, "%s not built yet; kmorph arm builds it\n", KMORPH_INITRD_PATH);
		return;
	}
	strftime(built, sizeof(built), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
	fprintf(out, "%s built %s, ", KMORPH_INITRD_PATH, built);
	check = popen(KMORPH_MKIMAGE " --check 2>/dev/null", "r");
	if (check) {
		if (!fgets(reason, sizeof(reason), check))
			reason[0] = '\0';
		status = pclose(check);
	}
	reason[strcspn(reason, "\n")] = '\0';
	if (status == 0)
		fprintf(out, "current\n");
	else if (status > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 1)
		fprintf(out, "stale (%s); kmorph arm rebuilds it\n", reason);
	else
		fprintf(out, "freshness unknown\n");
}

void image_describe(const struct kmorph_config *cfg, FILE *out)
{
	size_t i;

	if (cfg->initrd) {
		fprintf(out, "image: %s, the operator's own\n", cfg->initrd);
		return;
	}
	if (cfg->standby.count) {
		fprintf(out, "image: standby for");
		for (i = 0; i < cfg->standby.count; i++)
			fprintf(out, "%s %s", i ? "," : "", cfg->standby.items[i]);
		fprintf(out, ", ");
	} else
		fprintf(out, "image: distro, ");
	describe_built(out);
}

/*
 * kmorphd: runs inside the successor, probes the predecessor, and takes
 * the machine over when the predecessor falls silent.
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "kmorph/config.h"
#include "kmorph/devtree.h"
#include "kmorph/dump.h"
#include "kmorph/iomem.h"
#include "kmorph/log.h"
#include "kmorph/mkfs.h"
#include "kmorph/mksys.h"
#include "kmorph/probe.h"
#include "kmorph/request.h"
#include "kmorph/vsock.h"
#include "console.h"
#include "ops.h"
#include "takeover.h"

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define STATE_DIR "/run/kmorph"
#define STATE_PATH STATE_DIR "/state"

struct daemon {
	struct kmorph_config cfg;
	struct ops ops;
	struct takeover tk;
	int epfd;
	int timer_fd;
	int signal_fd;
	int listen_fd;
	int probe_fd;
	enum probe_result last_result;
	int last_probe_err;
	uint64_t last_probe_ms;
	bool probe_reported;
};

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void fill_status(const struct daemon *d, struct status_reply *r)
{
	memset(r, 0, sizeof(*r));
	snprintf(r->state, sizeof(r->state), "%s", takeover_state_name(d->tk.state));
	snprintf(r->predecessor, sizeof(r->predecessor), "%s",
		 probe_result_name(d->last_result));
	r->last_probe_ms = d->last_probe_ms ? now_ms() - d->last_probe_ms : 0;
	r->last_error = d->tk.last_error;
}

static void write_state_file(const struct daemon *d)
{
	struct status_reply r;
	char line[128], tmp[] = STATE_PATH ".tmp";
	int fd, n;

	fill_status(d, &r);
	n = request_format_status(&r, line, sizeof(line));
	if (n < 0)
		return;
	mkdir(STATE_DIR, 0755);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return;
	if (write(fd, line + 3, n - 3) == n - 3)
		rename(tmp, STATE_PATH);
	close(fd);
}

static void on_state(struct takeover *t, enum takeover_state state)
{
	struct daemon *d = container_of(t, struct daemon, tk);

	log_info("state %s", takeover_state_name(state));
	write_state_file(d);
	if (state == TK_TAKEN_OVER && d->cfg.console)
		console_start(d->cfg.console, d->cfg.console_baud, d->cfg.console_login);
}

static void deliver_probe(struct daemon *d, int err)
{
	enum probe_result r = probe_classify(err);

	if (r != d->last_result || !d->probe_reported ||
	    (r == PROBE_ERROR && err != d->last_probe_err)) {
		if (r == PROBE_ERROR)
			log_warn("probe failed locally: %s", strerror(err));
		else
			log_info("predecessor %s", probe_result_name(r));
		d->last_result = r;
		d->probe_reported = true;
		write_state_file(d);
	}
	d->last_probe_err = err;
	d->last_probe_ms = now_ms();
	if (r == PROBE_ALIVE)
		takeover_handle(&d->tk, TK_EV_PROBE_ALIVE);
	else if (r == PROBE_SILENT)
		takeover_handle(&d->tk, TK_EV_PROBE_SILENT);
}

static int epoll_add(int epfd, int fd, uint32_t events)
{
	struct epoll_event ev = { .events = events, .data.fd = fd };

	return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0 ? -errno : 0;
}

/* One probe per tick; a probe still pending at the next tick has timed out. */
static void on_tick(struct daemon *d)
{
	uint64_t expirations;
	int fd;

	if (read(d->timer_fd, &expirations, sizeof(expirations)) < 0)
		return;
	if (d->tk.state >= TK_FENCING)
		return;

	if (d->probe_fd >= 0) {
		close(d->probe_fd);
		d->probe_fd = -1;
		deliver_probe(d, ETIMEDOUT);
	}

	fd = probe_start(PROBE_CID_PREDECESSOR, PROBE_PORT);
	if (fd < 0) {
		deliver_probe(d, -fd);
		return;
	}
	if (epoll_add(d->epfd, fd, EPOLLOUT | EPOLLERR | EPOLLHUP)) {
		close(fd);
		deliver_probe(d, EIO);
		return;
	}
	d->probe_fd = fd;
}

static void on_probe_ready(struct daemon *d)
{
	int err = probe_finish(d->probe_fd);

	d->probe_fd = -1;
	deliver_probe(d, err);
}

static void answer_request(struct daemon *d, int fd)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	struct status_reply r;
	enum request_kind kind;
	char line[64], reply[128];
	ssize_t n;
	int len;

	if (poll(&pfd, 1, 500) <= 0)
		return;
	n = recv(fd, line, sizeof(line) - 1, MSG_DONTWAIT);
	if (n <= 0)
		return;
	line[n] = '\0';

	if (request_parse(line, &kind) || kind != REQUEST_STATUS) {
		len = snprintf(reply, sizeof(reply), "err unsupported request\n");
	} else {
		fill_status(d, &r);
		len = request_format_status(&r, reply, sizeof(reply));
	}
	if (len > 0)
		send(fd, reply, len, MSG_NOSIGNAL);
}

static void on_connection(struct daemon *d)
{
	int fd = accept4(d->listen_fd, NULL, NULL, SOCK_CLOEXEC);

	if (fd < 0)
		return;
	answer_request(d, fd);
	close(fd);
}

static int open_listener(void)
{
	struct sockaddr_vsock addr;
	int fd;

	fd = vsock_multikernel_socket(0);
	if (fd < 0)
		return fd;
	memset(&addr, 0, sizeof(addr));
	addr.svm_family = AF_VSOCK;
	addr.svm_cid = VSOCK_CID_ANY;
	addr.svm_port = KMORPH_VSOCK_PORT;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 4) < 0) {
		int err = errno;

		close(fd);
		return -err;
	}
	return fd;
}

static int open_timer(uint64_t interval_ms)
{
	struct itimerspec its;
	int fd;

	fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (fd < 0)
		return -errno;
	its.it_interval.tv_sec = interval_ms / 1000;
	its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000;
	its.it_value = its.it_interval;
	if (timerfd_settime(fd, 0, &its, NULL) < 0) {
		int err = errno;

		close(fd);
		return -err;
	}
	return fd;
}

static int open_signals(void)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
		return -errno;
	return signalfd(-1, &mask, SFD_CLOEXEC);
}

static int daemon_setup(struct daemon *d)
{
	static struct takeover_ops tk_ops;
	struct ops_env env = {
		.devtree_root = DEVTREE_ROOT,
		.sysfs_root = MKFS_ROOT,
		.iomem_path = IOMEM_PATH,
		.mem_path = DUMP_MEM_PATH,
		.block_size_path = MEMORY_BLOCK_SIZE_PATH,
		.dump_path = d->cfg.dump,
		.fence_fn = mksys_fence,
	};
	int ret;

	if (!devtree_has_host_tree(DEVTREE_ROOT)) {
		log_err("this kernel carries no host tree; kmorphd runs inside a "
			"successor armed with 'kmorph arm'");
		return -ENOENT;
	}
	ret = mkfs_mount(&(struct mkfs){ MKFS_ROOT });
	if (ret) {
		log_err("cannot mount %s: %s", MKFS_ROOT, strerror(-ret));
		return ret;
	}
	ret = ops_init(&d->ops, &env);
	if (ret)
		return ret;

	tk_ops = ops_takeover_base;
	tk_ops.on_state = on_state;
	takeover_init(&d->tk, &tk_ops, &d->ops, d->cfg.probe_timeouts, d->cfg.fence_retries);
	d->last_result = PROBE_ERROR;
	d->probe_fd = -1;

	d->epfd = epoll_create1(EPOLL_CLOEXEC);
	d->timer_fd = open_timer(d->cfg.probe_interval_ms);
	d->signal_fd = open_signals();
	d->listen_fd = open_listener();
	if (d->epfd < 0 || d->timer_fd < 0 || d->signal_fd < 0)
		return -errno;
	if (d->listen_fd < 0)
		log_warn("status requests unavailable: %s", strerror(-d->listen_fd));

	ret = epoll_add(d->epfd, d->timer_fd, EPOLLIN);
	if (!ret)
		ret = epoll_add(d->epfd, d->signal_fd, EPOLLIN);
	if (!ret && d->listen_fd >= 0)
		ret = epoll_add(d->epfd, d->listen_fd, EPOLLIN);
	return ret;
}

static void daemon_run(struct daemon *d)
{
	log_info("armed: probing the predecessor every %llu ms, fencing after %u silences",
		 (unsigned long long)d->cfg.probe_interval_ms, d->cfg.probe_timeouts);
	write_state_file(d);

	for (;;) {
		struct epoll_event ev;
		int n = epoll_wait(d->epfd, &ev, 1, -1);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			log_err("epoll_wait: %s", strerror(errno));
			return;
		}
		if (ev.data.fd == d->timer_fd)
			on_tick(d);
		else if (ev.data.fd == d->probe_fd)
			on_probe_ready(d);
		else if (ev.data.fd == d->listen_fd)
			on_connection(d);
		else if (ev.data.fd == d->signal_fd) {
			log_info("terminating");
			return;
		}
	}
}

static void daemonize(void)
{
	pid_t pid = fork();
	int null;

	if (pid < 0) {
		log_err("fork: %s", strerror(errno));
		exit(1);
	}
	if (pid > 0)
		exit(0);
	setsid();
	if (chdir("/") < 0)
		log_warn("chdir /: %s", strerror(errno));
	null = open("/dev/null", O_RDWR);
	if (null >= 0) {
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		close(null);
	}
	log_init("kmorphd", -1, -1);
	log_open_kmsg();
}

static void usage(void)
{
	fprintf(stderr, "usage: kmorphd [--config PATH] [--foreground]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ "foreground", no_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0 },
	};
	struct daemon d;
	const char *config_path = KMORPH_CONFIG_PATH;
	char err[128];
	int c, foreground = 0, ret;

	while ((c = getopt_long(argc, argv, "c:f", opts, NULL)) != -1) {
		switch (c) {
		case 'c': config_path = optarg; break;
		case 'f': foreground = 1; break;
		default: usage();
		}
	}
	if (optind != argc)
		usage();

	log_init("kmorphd", STDERR_FILENO, -1);
	log_open_kmsg();
	memset(&d, 0, sizeof(d));

	ret = config_load(config_path, &d.cfg, err, sizeof(err));
	if (ret == -ENOENT) {
		log_warn("%s; using defaults", err);
		ret = config_parse("", &d.cfg, err, sizeof(err));
	}
	if (ret) {
		log_err("config: %s", err);
		return 1;
	}

	if (!foreground)
		daemonize();

	ret = daemon_setup(&d);
	if (ret) {
		log_err("setup failed: %s", strerror(-ret));
		return 1;
	}
	daemon_run(&d);
	ops_free(&d.ops);
	config_free(&d.cfg);
	return 0;
}

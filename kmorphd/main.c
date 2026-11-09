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
#include <sys/prctl.h>
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
#include "kmorph/file.h"
#include "kmorph/log.h"
#include "kmorph/mkfs.h"
#include "kmorph/mksys.h"
#include "kmorph/netsnap.h"
#include "kmorph/pci.h"
#include "kmorph/probe.h"
#include "kmorph/request.h"
#include "kmorph/vsock.h"
#include "console.h"
#include "init.h"
#include "ops.h"
#include "standby.h"
#include "takeover.h"

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define STATE_DIR "/run/kmorph"
#define STATE_PATH STATE_DIR "/state"
/* Whatever runs next, an init's path unit or a script, waits for this file to appear. */
#define TAKEN_OVER_PATH STATE_DIR "/taken-over"

struct daemon {
	bool init;
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
	bool handed_over;
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
		 d->handed_over ? "handed-over" : probe_result_name(d->last_result));
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

static void mark_taken_over(void)
{
	int fd = open(TAKEN_OVER_PATH, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);

	if (fd < 0)
		log_err("cannot create %s: %s", TAKEN_OVER_PATH, strerror(errno));
	else
		close(fd);
}

static int ready_fd = -1;

static void report_taken_over(void)
{
	if (ready_fd < 0)
		return;
	if (write(ready_fd, "", 1) != 1)
		log_err("cannot report the takeover to the parent: %s", strerror(errno));
	close(ready_fd);
	ready_fd = -1;
}

static void on_state(struct takeover *t, enum takeover_state state)
{
	struct daemon *d = container_of(t, struct daemon, tk);

	log_info("state %s", takeover_state_name(state));
	write_state_file(d);
	if (state != TK_TAKEN_OVER)
		return;
	report_taken_over();
	if (d->cfg.console_login)
		console_start(d->cfg.console, d->cfg.console_baud, d->cfg.console_login);
	mark_taken_over();
}

static void deliver_probe(struct daemon *d, int err)
{
	enum probe_result r = probe_classify(err);
	enum takeover_state before = d->tk.state;
	bool changed = r != d->last_result || !d->probe_reported ||
		       (r == PROBE_ERROR && err != d->last_probe_err);

	if (changed) {
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

	/*
	 * The kernel drops /dev/kmsg lines past ten in five seconds, which a
	 * takeover nears, so a result the new state already tells is not logged.
	 */
	if (!changed)
		return;
	if (r == PROBE_ERROR)
		log_warn("probe failed locally: %s", strerror(err));
	else if (d->tk.state == before)
		log_info("predecessor %s", probe_result_name(r));
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

/* Only a live predecessor that is not already suspected may hand over. */
static int answer_handover(const struct daemon *d, char *reply, size_t len, bool *accepted)
{
	char reason[64];

	*accepted = d->tk.state == TK_ARMED && d->last_result == PROBE_ALIVE;
	snprintf(reason, sizeof(reason), "state=%s predecessor=%s",
		 takeover_state_name(d->tk.state), probe_result_name(d->last_result));
	return request_format_handover(*accepted ? NULL : reason, reply, len);
}

static struct netsnap network;

/* What the predecessor sees now replaces what it saw at arm time, until the fence. */
static int answer_network(struct daemon *d, const char *text, char *reply, size_t len)
{
	static struct netsnap next;
	char reason[64];

	if (d->tk.state >= TK_FENCING) {
		snprintf(reason, sizeof(reason), "state=%s", takeover_state_name(d->tk.state));
		return request_format_network_reply(reason, reply, len);
	}
	if (netsnap_parse(text, &next))
		return request_format_network_reply("malformed", reply, len);
	network = next;
	d->ops.env.network = &network;
	/* A restarted kmorphd picks the latest identity up from here */
	if (file_write(STANDBY_NETWORK, text, strlen(text)))
		log_warn("network: cannot keep the identity in %s", STANDBY_NETWORK);
	log_info("network: identity of %zu interfaces updated by the predecessor", network.count);
	return request_format_network_reply(NULL, reply, len);
}

#define REQUEST_WAIT_MS	500
#define REQUEST_LINE_MAX 64

/* The request line and its payload, NUL terminated; the line length, or -errno. */
static int read_request(int fd, char *buf, size_t cap, enum request_kind *kind, size_t *payload)
{
	uint64_t deadline = now_ms() + REQUEST_WAIT_MS;
	size_t got = 0, line = 0;

	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		uint64_t now = now_ms();
		char *nl;
		ssize_t n;

		if (now >= deadline || poll(&pfd, 1, deadline - now) <= 0)
			return -ETIMEDOUT;
		n = recv(fd, buf + got, cap - 1 - got, MSG_DONTWAIT);
		if (n <= 0)
			return n ? -errno : -ECONNRESET;
		got += n;
		buf[got] = '\0';
		if (!line) {
			char head[REQUEST_LINE_MAX + 1];

			nl = memchr(buf, '\n', got);
			if (!nl)
				continue;
			line = nl - buf + 1;
			if (line > REQUEST_LINE_MAX)
				return -EINVAL;
			memcpy(head, buf, line);
			head[line] = '\0';
			if (request_parse(head, kind, payload))
				return -EINVAL;
			if (line + *payload >= cap)
				return -E2BIG;
		}
		if (got >= line + *payload) {
			buf[line + *payload] = '\0';
			return line;
		}
	}
}

/* Returns whether the predecessor handed over. */
static bool answer_request(struct daemon *d, int fd)
{
	static char buf[REQUEST_LINE_MAX + NETSNAP_TEXT_MAX];
	struct status_reply r;
	enum request_kind kind;
	bool accepted = false;
	char reply[128];
	size_t payload;
	int len, line;

	line = read_request(fd, buf, sizeof(buf), &kind, &payload);
	if (line == -ETIMEDOUT || line == -ECONNRESET)
		return false;
	if (line < 0) {
		len = snprintf(reply, sizeof(reply), "err unsupported request\n");
	} else if (kind == REQUEST_HANDOVER) {
		len = answer_handover(d, reply, sizeof(reply), &accepted);
	} else if (kind == REQUEST_NETWORK) {
		len = answer_network(d, buf + line, reply, sizeof(reply));
	} else {
		fill_status(d, &r);
		len = request_format_status(&r, reply, sizeof(reply));
	}
	if (len > 0)
		send(fd, reply, len, MSG_NOSIGNAL);
	return accepted;
}

/* Nothing crashed, so the predecessor's memory is taken over without a dump. */
static void take_handover(struct daemon *d)
{
	log_info("predecessor hands over");
	d->handed_over = true;
	d->ops.env.dump_path = NULL;
	takeover_handle(&d->tk, TK_EV_REQUEST);
}

static void on_connection(struct daemon *d)
{
	int fd = accept4(d->listen_fd, NULL, NULL, SOCK_CLOEXEC);
	bool handover;

	if (fd < 0)
		return;
	handover = answer_request(d, fd);
	/* The answer leaves before the fence stops the predecessor reading it */
	close(fd);
	if (handover)
		take_handover(d);
}

static int open_listener(void)
{
	struct sockaddr_vsock addr;
	int fd;

	/* A wakeup with no connection left to accept must not stall the probes */
	fd = vsock_multikernel_socket(SOCK_NONBLOCK);
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

static int open_signals(bool init)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	if (init)
		sigaddset(&mask, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
		return -errno;
	return signalfd(-1, &mask, SFD_CLOEXEC);
}

/* The standby root keeps the forwarded identity in /run, the image's own in /etc. */
static const struct netsnap *load_network(void)
{
	static const char *const paths[] = { STANDBY_NETWORK, NETSNAP_PATH };
	struct netsnap *snap = &network;
	char *text;
	size_t i;
	int ret;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		if (file_read_string(paths[i], &text))
			continue;
		ret = netsnap_parse(text, snap);
		free(text);
		if (ret) {
			log_warn("network: %s is malformed; the NICs wait for the disk's config",
				 paths[i]);
			return NULL;
		}
		return snap->count ? snap : NULL;
	}
	return NULL;
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
		.pci_bus = PCI_BUS_ROOT,
		.reset_devices = d->cfg.reset_devices,
		.fence_fn = mksys_fence,
		.network = load_network(),
		.net_env = &netsnap_default_env,
		.net_restored_path = STATE_DIR "/network-restored",
		.next_memory = d->cfg.memory,
		.pool_claim_path = POOL_CLAIM_PATH,
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

	/* A dump reader that goes away fails the dump, not the takeover */
	signal(SIGPIPE, SIG_IGN);
	d->epfd = epoll_create1(EPOLL_CLOEXEC);
	d->timer_fd = open_timer(d->cfg.probe_interval_ms);
	d->signal_fd = open_signals(d->init);
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
			struct signalfd_siginfo si;

			if (read(d->signal_fd, &si, sizeof(si)) != sizeof(si))
				continue;
			if (si.ssi_signo == SIGCHLD) {
				init_reap();
				continue;
			}
			/* PID 1 must not exit: the kernel panics without an init. */
			if (d->init) {
				log_warn("ignoring signal %u as init", si.ssi_signo);
				continue;
			}
			log_info("terminating");
			return;
		}
	}
}

static int wait_for_takeover(int pipefd[2])
{
	ssize_t n;
	char done;

	close(pipefd[1]);
	do
		n = read(pipefd[0], &done, 1);
	while (n < 0 && errno == EINTR);
	return n == 1 ? 0 : 1;
}

/* With wait, the first process exits only once the takeover is done, so its caller waits too. */
static void daemonize(bool wait)
{
	int pipefd[2], null;
	pid_t pid;

	if (wait && pipe2(pipefd, O_CLOEXEC) < 0) {
		log_err("pipe: %s", strerror(errno));
		exit(1);
	}
	pid = fork();
	if (pid < 0) {
		log_err("fork: %s", strerror(errno));
		exit(1);
	}
	if (pid > 0)
		exit(wait ? wait_for_takeover(pipefd) : 0);
	if (wait) {
		close(pipefd[0]);
		ready_fd = pipefd[1];
	}
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
	fprintf(stderr, "usage: kmorphd [--config PATH] [--foreground | --wait]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ "foreground", no_argument, NULL, 'f' },
		{ "wait", no_argument, NULL, 'w' },
		{ NULL, 0, NULL, 0 },
	};
	struct daemon d;
	const char *config_path = KMORPH_CONFIG_PATH;
	char err[128];
	int c, foreground = 0, wait = 0, ret;

	while ((c = getopt_long(argc, argv, "c:fw", opts, NULL)) != -1) {
		switch (c) {
		case 'c': config_path = optarg; break;
		case 'f': foreground = 1; break;
		case 'w': wait = 1; break;
		default: usage();
		}
	}
	if (optind != argc || (foreground && wait))
		usage();

	/* As init nothing is mounted yet, not even the /dev the kernel log is reached through. */
	memset(&d, 0, sizeof(d));
	d.init = getpid() == 1;
	ret = d.init ? init_mount_filesystems(&init_default_env) : 0;
	log_init("kmorphd", STDERR_FILENO, -1);
	log_open_kmsg();
	if (ret)
		log_warn("init: some filesystems did not mount: %s", strerror(-ret));
	if (d.init && access(STANDBY_IMAGE, F_OK) == 0) {
		ret = standby_enter(&standby_default_env, config_path);
		if (ret >= 0)
			config_path = STANDBY_CONFIG;
		if (ret == 0) {
			/* It runs as the image's /init, which is what ps would call it */
			prctl(PR_SET_NAME, "kmorphd");
			d.init = false;
			foreground = 1;
		}
	}

	ret = config_load(config_path, &d.cfg, err, sizeof(err));
	if (ret == -ENOENT) {
		log_warn("%s; using defaults", err);
		ret = config_parse("", &d.cfg, err, sizeof(err));
	}
	if (ret) {
		log_err("config: %s", err);
		return 1;
	}

	if (!foreground && !d.init)
		daemonize(wait);

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

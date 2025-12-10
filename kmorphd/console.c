#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kmorph/log.h"
#include "kmorph/serial.h"
#include "console.h"

int console_getty_argv(const char *tty, unsigned int baud, const char *login,
		       char **argv, int max)
{
	char speed[16];
	int n = 0;

	if (max < (login ? 9 : 6))
		return -ENOSPC;
	snprintf(speed, sizeof(speed), "%u", baud);
	argv[n++] = strdup("getty");
	argv[n++] = strdup("-L");
	if (login) {
		argv[n++] = strdup("-n");
		argv[n++] = strdup("-l");
		argv[n++] = strdup(login);
	}
	argv[n++] = strdup(speed);
	argv[n++] = strdup(tty);
	argv[n++] = strdup("vt100");
	argv[n] = NULL;
	return n;
}

void console_getty_argv_free(char **argv)
{
	int i;

	for (i = 0; argv[i]; i++)
		free(argv[i]);
}

static pid_t console_start_getty(const char *tty, unsigned int baud, const char *login);

static void supervise(char **argv)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
	for (;;) {
		pid_t pid = fork();
		int status;

		if (pid == 0) {
			setsid();
			execvp(argv[0], argv);
			_exit(127);
		}
		if (pid < 0 || waitpid(pid, &status, 0) < 0)
			_exit(1);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
			_exit(127);
		sleep(1);
	}
}

int console_poll_mode(const char *tty)
{
	struct serial_struct ss;
	char path[64];
	int fd, ret = 0;

	snprintf(path, sizeof(path), "/dev/%s", tty);
	fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	if (ioctl(fd, TIOCGSERIAL, &ss) < 0) {
		ret = -errno;
	} else if (ss.irq) {
		ss.irq = 0;
		if (ioctl(fd, TIOCSSERIAL, &ss) < 0)
			ret = -errno;
	}
	close(fd);
	return ret;
}

pid_t console_start(const char *tty, unsigned int baud, const char *login)
{
	int ret = console_poll_mode(tty);

	if (ret)
		log_warn("console: cannot switch %s to polling: %s", tty, strerror(-ret));
	else
		log_info("console: %s driven by polling", tty);
	return console_start_getty(tty, baud, login);
}

static pid_t console_start_getty(const char *tty, unsigned int baud, const char *login)
{
	char *argv[10];
	pid_t pid;

	if (console_getty_argv(tty, baud, login, argv, 10) < 0)
		return -ENOSPC;
	pid = fork();
	if (pid == 0)
		supervise(argv);
	console_getty_argv_free(argv);
	if (pid < 0)
		return -errno;
	log_info("console: getty supervisor on %s at %u baud (pid %d)", tty, baud, pid);
	return pid;
}

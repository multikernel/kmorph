#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "kmorph/log.h"
#include "kmorph/serial.h"
#include "console.h"

static pid_t console_start_supervisor(const char *tty, unsigned int baud, const char *login);

int console_line_attrs(struct termios *t, unsigned int baud)
{
	static const struct { unsigned int rate; speed_t code; } speeds[] = {
		{ 9600, B9600 }, { 19200, B19200 }, { 38400, B38400 },
		{ 57600, B57600 }, { 115200, B115200 }, { 230400, B230400 },
	};
	size_t i;

	for (i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++)
		if (speeds[i].rate == baud)
			break;
	if (i == sizeof(speeds) / sizeof(speeds[0]))
		return -EINVAL;
	memset(t, 0, sizeof(*t));
	cfsetispeed(t, speeds[i].code);
	cfsetospeed(t, speeds[i].code);
	t->c_cflag |= CS8 | CREAD | HUPCL | CLOCAL;
	t->c_iflag = ICRNL;
	t->c_oflag = OPOST | ONLCR;
	t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
	t->c_cc[VINTR] = 3;
	t->c_cc[VERASE] = 0177;
	t->c_cc[VKILL] = 025;
	t->c_cc[VEOF] = 4;
	t->c_cc[VMIN] = 1;
	return 0;
}

/* The child: open the line as its terminal and become the login program. */
static void run_login(const char *tty, unsigned int baud, const char *login)
{
	struct termios t;
	char path[64];
	int fd;

	snprintf(path, sizeof(path), "/dev/%s", tty);
	setsid();
	fd = open(path, O_RDWR | O_NOCTTY);
	if (fd < 0)
		_exit(1);
	if (!console_line_attrs(&t, baud))
		tcsetattr(fd, TCSANOW, &t);
	ioctl(fd, TIOCSCTTY, 0);
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > STDERR_FILENO)
		close(fd);
	setenv("TERM", "vt100", 1);
	execl(login, login, (char *)NULL);
	_exit(127);
}

static void supervise(const char *tty, unsigned int baud, const char *login)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);
	for (;;) {
		pid_t pid = fork();
		int status;

		if (pid == 0)
			run_login(tty, baud, login);
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
	struct termios t;
	int ret = console_poll_mode(tty);

	if (ret)
		log_warn("console: cannot switch %s to polling: %s", tty, strerror(-ret));
	else
		log_info("console: %s driven by polling", tty);
	if (console_line_attrs(&t, baud))
		log_warn("console: %u baud is not a termios rate; the line keeps its speed", baud);
	return console_start_supervisor(tty, baud, login);
}

static pid_t console_start_supervisor(const char *tty, unsigned int baud, const char *login)
{
	pid_t pid = fork();

	if (pid == 0)
		supervise(tty, baud, login);
	if (pid < 0)
		return -errno;
	log_info("console: supervising %s on %s at %u baud (pid %d)", login, tty, baud, pid);
	return pid;
}

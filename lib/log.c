#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kmorph/log.h"

#define KMSG_INFO 6
#define KMSG_WARNING 4
#define KMSG_ERR 3

static const char *log_ident = "kmorph";
static int log_stderr_fd = STDERR_FILENO;
static int log_kmsg_fd = -1;

void log_init(const char *ident, int stderr_fd, int kmsg_fd)
{
	log_ident = ident;
	log_stderr_fd = stderr_fd;
	log_kmsg_fd = kmsg_fd;
}

void log_open_kmsg(void)
{
	log_kmsg_fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
}

static void write_all(int fd, const char *s, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, s, len);

		if (n <= 0)
			return;
		s += n;
		len -= n;
	}
}

static void log_line(int priority, const char *level, const char *fmt, va_list ap)
{
	char msg[1024], line[1200];
	int n;

	vsnprintf(msg, sizeof(msg), fmt, ap);
	if (log_stderr_fd >= 0) {
		n = snprintf(line, sizeof(line), "%s: %s%s\n", log_ident, level, msg);
		write_all(log_stderr_fd, line, n);
	}
	if (log_kmsg_fd >= 0) {
		n = snprintf(line, sizeof(line), "<%d>%s: %s%s\n", priority, log_ident, level, msg);
		write_all(log_kmsg_fd, line, n);
	}
}

#define LOG_FN(name, priority, level) \
void name(const char *fmt, ...) \
{ \
	va_list ap; \
	va_start(ap, fmt); \
	log_line(priority, level, fmt, ap); \
	va_end(ap); \
}

LOG_FN(log_info, KMSG_INFO, "")
LOG_FN(log_warn, KMSG_WARNING, "warning: ")
LOG_FN(log_err, KMSG_ERR, "error: ")

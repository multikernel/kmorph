#ifndef KMORPH_LOG_H
#define KMORPH_LOG_H

/*
 * Lines go to stderr and, when a kmsg fd is open, to the kernel log with
 * a syslog priority prefix so they survive alongside the kernel's own
 * takeover messages. Either fd may be -1.
 */
void log_init(const char *ident, int stderr_fd, int kmsg_fd);
void log_open_kmsg(void);

void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif

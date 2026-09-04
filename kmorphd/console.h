#ifndef KMORPHD_CONSOLE_H
#define KMORPHD_CONSOLE_H

#include <sys/types.h>
#include <termios.h>

/*
 * After a takeover the successor owns the serial line it was armed with.
 * kmorphd puts the configured login program on it, doing the little a
 * getty would: speed, line discipline, controlling terminal. A
 * supervisor keeps the program respawning.
 */

/* 8N1, local, cooked, at baud; -EINVAL for a rate termios has no code for. */
int console_line_attrs(struct termios *t, unsigned int baud);

/*
 * Drive the line by polling: a successor has no interrupt routing for
 * legacy lines, and the 8250 driver polls a port whose IRQ is 0.
 */
int console_poll_mode(const char *tty);

/* Start the supervisor; returns its pid or -errno. */
pid_t console_start(const char *tty, unsigned int baud, const char *login);

#endif

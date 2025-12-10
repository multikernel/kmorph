#ifndef KMORPHD_CONSOLE_H
#define KMORPHD_CONSOLE_H

#include <sys/types.h>

/*
 * After a takeover the successor owns the serial line it was armed with.
 * A getty on it gives the operator the console back; a supervisor keeps
 * the getty respawning.
 */

/* Fill argv for busybox/agetty-style getty: returns argc, or -ENOSPC. */
int console_getty_argv(const char *tty, unsigned int baud, const char *login,
		       char **argv, int max);
void console_getty_argv_free(char **argv);

/*
 * Drive the line by polling: a successor has no interrupt routing for
 * legacy lines, and the 8250 driver polls a port whose IRQ is 0.
 */
int console_poll_mode(const char *tty);

/* Start the supervisor; returns its pid or -errno. */
pid_t console_start(const char *tty, unsigned int baud, const char *login);

#endif

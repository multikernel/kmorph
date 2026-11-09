#ifndef KMORPH_UART_H
#define KMORPH_UART_H

#include <stddef.h>

/*
 * The UART behind a serial line, as earlycon needs it: earlycon writes
 * to the registers directly, so the successor's kernel messages reach
 * the line without a driver or an interrupt.
 */

enum uart_family { UART_8250, UART_PL011 };

struct uart_line {
	enum uart_family family;
	const char *access;	/* earlycon's register access: io, mmio, mmio16, mmio32, mmio32be */
	unsigned long addr;
};

/*
 * Reads class_root/tty/<tty>. -ENOENT: no such tty; -ENODEV: a placeholder
 * port with no UART behind it; -EOPNOTSUPP: a UART earlycon cannot drive.
 */
int uart_read(const char *class_root, const char *tty, struct uart_line *out);

/* The earlycon= parameter for the line at baud. */
int uart_earlycon(const struct uart_line *u, unsigned int baud, char *out, size_t len);

/* cmdline's earlycon= parameter, when it names a UART by address; else -ENOENT. */
int uart_cmdline_earlycon(const char *cmdline, char *out, size_t len);

#endif

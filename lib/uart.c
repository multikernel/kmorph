#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/file.h"
#include "kmorph/uart.h"

/* From uapi/linux/serial_core.h */
#define PORT_UNKNOWN		0
#define PORT_MAX_8250_FAMILY	30
#define PORT_AMBA		32

static int read_ulong(const char *dir, const char *attr, unsigned long *v)
{
	char path[PATH_MAX], *s, *end;
	int ret;

	snprintf(path, sizeof(path), "%s/%s", dir, attr);
	ret = file_read_string(path, &s);
	if (ret)
		return ret;
	errno = 0;
	*v = strtoul(s, &end, 0);
	ret = errno || end == s || *end ? -EINVAL : 0;
	free(s);
	return ret;
}

/* Indexed by the serial_struct io_type */
static const char *const accesses[] = {
	[0] = "io", [2] = "mmio", [3] = "mmio32", [6] = "mmio32be", [7] = "mmio16",
};

int uart_read(const char *class_root, const char *tty, struct uart_line *out)
{
	char dir[PATH_MAX];
	unsigned long type, io_type;
	int ret;

	if (!*tty || strchr(tty, '/'))
		return -EINVAL;
	snprintf(dir, sizeof(dir), "%s/tty/%s", class_root, tty);
	ret = read_ulong(dir, "type", &type);
	if (ret)
		return ret;
	if (type == PORT_UNKNOWN)
		return -ENODEV;
	if (type == PORT_AMBA)
		out->family = UART_PL011;
	else if (type <= PORT_MAX_8250_FAMILY)
		out->family = UART_8250;
	else
		return -EOPNOTSUPP;

	ret = read_ulong(dir, "io_type", &io_type);
	if (ret)
		return ret;
	if (io_type >= sizeof(accesses) / sizeof(accesses[0]) || !accesses[io_type])
		return -EOPNOTSUPP;
	out->access = accesses[io_type];
	return read_ulong(dir, io_type ? "iomem_base" : "port", &out->addr);
}

int uart_earlycon(const struct uart_line *u, unsigned int baud, char *out, size_t len)
{
	const char *name = u->family == UART_PL011 ? "pl011" : "uart8250";
	int n = snprintf(out, len, "earlycon=%s,%s,0x%lx,%u", name, u->access, u->addr, baud);

	return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

int uart_cmdline_earlycon(const char *cmdline, char *out, size_t len)
{
	static const char key[] = "earlycon=";
	const char *p = cmdline;

	while ((p = strstr(p, key))) {
		size_t n = strcspn(p, " \n");

		if ((p == cmdline || p[-1] == ' ') && memchr(p, ',', n)) {
			if (n >= len)
				return -ENAMETOOLONG;
			memcpy(out, p, n);
			out[n] = '\0';
			return 0;
		}
		p += n;
	}
	return -ENOENT;
}

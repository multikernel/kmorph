#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test.h"
#include "kmorph/file.h"
#include "kmorph/uart.h"

static char root[] = "/tmp/kmorph-test-uart-XXXXXX";

static void put_port(const char *tty, const char *type, const char *io_type,
		     const char *iomem_base, const char *port)
{
	char path[600];

	snprintf(path, sizeof(path), "%s/tty", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/tty/%s", root, tty);
	mkdir(path, 0755);
#define PUT(attr, value) do { \
	snprintf(path, sizeof(path), "%s/tty/%s/" attr, root, tty); \
	CHECK_EQ(file_write(path, value, strlen(value)), 0); \
} while (0)
	PUT("type", type);
	PUT("io_type", io_type);
	PUT("iomem_base", iomem_base);
	PUT("port", port);
#undef PUT
}

static void pl011_gets_a_pl011_earlycon(void)
{
	struct uart_line u;
	char out[128];

	put_port("ttyAMA0", "32\n", "3\n", "0x94080000\n", "0x0\n");
	CHECK_EQ(uart_read(root, "ttyAMA0", &u), 0);
	CHECK_EQ(u.family, UART_PL011);
	CHECK_EQ(uart_earlycon(&u, 115200, out, sizeof(out)), 0);
	CHECK_STREQ(out, "earlycon=pl011,mmio32,0x94080000,115200");
}

static void legacy_8250_uses_its_io_port(void)
{
	struct uart_line u;
	char out[128];

	put_port("ttyS0", "4\n", "0\n", "0x0\n", "0x3F8\n");
	CHECK_EQ(uart_read(root, "ttyS0", &u), 0);
	CHECK_EQ(u.family, UART_8250);
	CHECK_EQ(uart_earlycon(&u, 9600, out, sizeof(out)), 0);
	CHECK_STREQ(out, "earlycon=uart8250,io,0x3f8,9600");
}

static void placeholder_port_is_refused(void)
{
	struct uart_line u;

	put_port("ttyS1", "0\n", "0\n", "0x0\n", "0x0\n");
	CHECK_EQ(uart_read(root, "ttyS1", &u), -ENODEV);
}

static void unknown_uart_or_access_is_refused(void)
{
	struct uart_line u;

	put_port("ttyX0", "91\n", "2\n", "0x1000\n", "0x0\n");
	CHECK_EQ(uart_read(root, "ttyX0", &u), -EOPNOTSUPP);
	put_port("ttyS2", "4\n", "4\n", "0x1000\n", "0x0\n");
	CHECK_EQ(uart_read(root, "ttyS2", &u), -EOPNOTSUPP);
}

static void missing_tty_is_enoent(void)
{
	struct uart_line u;

	CHECK_EQ(uart_read(root, "ttyAMA9", &u), -ENOENT);
	CHECK_EQ(uart_read(root, "../tty", &u), -EINVAL);
}

static void cmdline_earlycon_needs_an_address(void)
{
	char out[128];

	CHECK_EQ(uart_cmdline_earlycon("ro earlycon=pl011,mmio32,0x9000000 quiet", out, sizeof(out)), 0);
	CHECK_STREQ(out, "earlycon=pl011,mmio32,0x9000000");
	CHECK_EQ(uart_cmdline_earlycon("earlycon quiet", out, sizeof(out)), -ENOENT);
	CHECK_EQ(uart_cmdline_earlycon("xearlycon=a,b", out, sizeof(out)), -ENOENT);
	CHECK_EQ(uart_cmdline_earlycon("earlycon=uart8250,io,0x3f8\n", out, sizeof(out)), 0);
	CHECK_STREQ(out, "earlycon=uart8250,io,0x3f8");
	CHECK_EQ(uart_cmdline_earlycon("earlycon=pl011,mmio32,0x9000000", out, 8), -ENAMETOOLONG);
}

TEST_MAIN({
	char cmd[128];

	if (!mkdtemp(root)) {
		perror("mkdtemp");
		return 1;
	}
	RUN(pl011_gets_a_pl011_earlycon);
	RUN(legacy_8250_uses_its_io_port);
	RUN(placeholder_port_is_refused);
	RUN(unknown_uart_or_access_is_refused);
	RUN(missing_tty_is_enoent);
	RUN(cmdline_earlycon_needs_an_address);
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	CHECK_EQ(system(cmd), 0);
})

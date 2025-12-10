#ifndef KMORPH_SERIAL_H
#define KMORPH_SERIAL_H

/*
 * From uapi/linux/serial.h, carried here so a libc without kernel
 * headers (musl) still builds. TIOCGSERIAL and TIOCSSERIAL come from
 * <sys/ioctl.h> on both glibc and musl.
 */
struct serial_struct {
	int type;
	int line;
	unsigned int port;
	int irq;
	int flags;
	int xmit_fifo_size;
	int custom_divisor;
	int baud_base;
	unsigned short close_delay;
	char io_type;
	char reserved_char[1];
	int hub6;
	unsigned short closing_wait;
	unsigned short closing_wait2;
	unsigned char *iomem_base;
	unsigned short iomem_reg_shift;
	unsigned int port_high;
	unsigned long iomap_base;
};

#endif

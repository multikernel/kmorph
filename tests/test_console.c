#include <termios.h>

#include "test.h"
#include "console.h"

static void the_line_is_8n1_local_and_cooked_at_the_configured_speed(void)
{
	struct termios t;

	CHECK_EQ(console_line_attrs(&t, 115200), 0);
	CHECK_EQ(cfgetospeed(&t), B115200);
	CHECK_EQ(cfgetispeed(&t), B115200);
	CHECK_EQ(t.c_cflag & CSIZE, CS8);
	CHECK_EQ(t.c_cflag & PARENB, 0);
	CHECK_EQ(t.c_cflag & CSTOPB, 0);
	CHECK(t.c_cflag & CLOCAL);
	CHECK(t.c_cflag & CREAD);
	CHECK(t.c_lflag & ICANON);
	CHECK(t.c_lflag & ECHO);
	CHECK(t.c_lflag & ISIG);
	CHECK(t.c_iflag & ICRNL);
	CHECK(t.c_oflag & ONLCR);
}

static void every_common_speed_is_known(void)
{
	struct termios t;

	CHECK_EQ(console_line_attrs(&t, 9600), 0);
	CHECK_EQ(cfgetospeed(&t), B9600);
	CHECK_EQ(console_line_attrs(&t, 38400), 0);
	CHECK_EQ(cfgetospeed(&t), B38400);
	CHECK_EQ(console_line_attrs(&t, 230400), 0);
	CHECK_EQ(cfgetospeed(&t), B230400);
}

static void an_unsupported_speed_is_rejected(void)
{
	struct termios t;

	CHECK_EQ(console_line_attrs(&t, 12345), -EINVAL);
}

TEST_MAIN({
	RUN(the_line_is_8n1_local_and_cooked_at_the_configured_speed);
	RUN(every_common_speed_is_known);
	RUN(an_unsupported_speed_is_rejected);
})

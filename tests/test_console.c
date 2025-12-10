#include "test.h"
#include "console.h"

static void getty_line_uses_the_configured_tty_and_baud(void)
{
	char *argv[8];
	int n;

	n = console_getty_argv("ttyS0", 115200, NULL, argv, 8);
	CHECK_EQ(n, 5);
	CHECK_STREQ(argv[0], "getty");
	CHECK_STREQ(argv[1], "-L");
	CHECK_STREQ(argv[2], "115200");
	CHECK_STREQ(argv[3], "ttyS0");
	CHECK_STREQ(argv[4], "vt100");
	CHECK(argv[5] == NULL);
	console_getty_argv_free(argv);
}

static void login_program_skips_the_prompt(void)
{
	char *argv[10];
	int n;

	n = console_getty_argv("ttyS0", 9600, "/bin/sh", argv, 10);
	CHECK_EQ(n, 8);
	CHECK_STREQ(argv[1], "-L");
	CHECK_STREQ(argv[2], "-n");
	CHECK_STREQ(argv[3], "-l");
	CHECK_STREQ(argv[4], "/bin/sh");
	CHECK_STREQ(argv[5], "9600");
	CHECK_STREQ(argv[6], "ttyS0");
	console_getty_argv_free(argv);
}

static void too_small_argv_is_rejected(void)
{
	char *argv[4];

	CHECK(console_getty_argv("ttyS0", 115200, NULL, argv, 4) < 0);
}

TEST_MAIN({
	RUN(getty_line_uses_the_configured_tty_and_baud);
	RUN(login_program_skips_the_prompt);
	RUN(too_small_argv_is_rejected);
})

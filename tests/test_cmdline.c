#include <stdlib.h>

#include "test.h"
#include "kmorph/cmdline.h"

static void words_split_at_blanks_outside_quotes(void)
{
	struct cmdline c;

	CHECK_EQ(cmdline_split("  root=/dev/sda1 ro\tdyndbg=\"file a.c +p\" quiet\n", &c), 0);
	CHECK_EQ(c.count, 4);
	CHECK_STREQ(c.words[0], "root=/dev/sda1");
	CHECK_STREQ(c.words[1], "ro");
	CHECK_STREQ(c.words[2], "dyndbg=\"file a.c +p\"");
	CHECK_STREQ(c.words[3], "quiet");
	cmdline_free(&c);
	CHECK_EQ(cmdline_split("   ", &c), 0);
	CHECK_EQ(c.count, 0);
	CHECK(cmdline_join(&c) == NULL);
}

static void names_match_whole_and_before_the_equals(void)
{
	struct cmdline c;
	char *s;

	CHECK_EQ(cmdline_split("mem=4G memmap=1G$4G console=tty0 console=ttyAMA0 quiet", &c), 0);
	CHECK(cmdline_has(&c, "mem"));
	CHECK(cmdline_has(&c, "quiet"));
	CHECK(!cmdline_has(&c, "me"));
	CHECK(cmdline_has(&c, "quiet=1"));
	cmdline_remove(&c, "mem=8G");
	cmdline_remove(&c, "console");
	s = cmdline_join(&c);
	CHECK_STREQ(s, "memmap=1G$4G quiet");
	free(s);
	cmdline_free(&c);
}

static void add_keeps_a_word_already_named(void)
{
	struct cmdline c;
	char *s;

	CHECK_EQ(cmdline_split("rd.timeout=5", &c), 0);
	CHECK_EQ(cmdline_add(&c, "rd.timeout=0"), 0);
	CHECK_EQ(cmdline_add(&c, "rd.retry=2147483647"), 0);
	CHECK_EQ(cmdline_append(&c, "console=ttyS0"), 0);
	s = cmdline_join(&c);
	CHECK_STREQ(s, "rd.timeout=5 rd.retry=2147483647 console=ttyS0");
	free(s);
	cmdline_free(&c);
}

TEST_MAIN({
	RUN(words_split_at_blanks_outside_quotes);
	RUN(names_match_whole_and_before_the_equals);
	RUN(add_keeps_a_word_already_named);
})

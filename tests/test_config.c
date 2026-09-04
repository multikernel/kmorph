#include <unistd.h>

#include "test.h"
#include "kmorph/config.h"

static void defaults_apply_when_keys_are_absent(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK_EQ(config_parse("", &c, err, sizeof(err)), 0);
	CHECK_STREQ(c.name, "successor");
	CHECK_EQ(c.probe_interval_ms, 100);
	CHECK_EQ(c.probe_timeouts, 5);
	CHECK_EQ(c.fence_retries, 3);
	CHECK(c.dump == NULL);
	CHECK(c.kernel == NULL);
	CHECK_EQ(c.cpus.count, 0);
	CHECK_EQ(c.devices.count, 0);
	CHECK(c.console == NULL);
	CHECK_EQ(c.console_baud, 115200);
	CHECK(c.console_login == NULL);
	config_free(&c);
}

static void parses_every_key_with_comments_and_spacing(void)
{
	const char *text =
		"# successor\n"
		"name = backup\n"
		"cpus=12-15,20\n"
		"  memory   = 4GB  \n"
		"kernel = /boot/vmlinuz\n"
		"initrd = /boot/successor.img\n"
		"cmdline = console=ttyS0 quiet\n"
		"\n"
		"probe_interval = 250ms\n"
		"probe_timeouts = 8\n"
		"fence_retries = 1\n"
		"dump = /var/crash/predecessor.raw\n"
		"devices = 0000:09:00.0, 0000:0a:00.0\n"
		"machine_cpus = 0-3\n"
		"console = ttyS0\n"
		"console_baud = 9600\n"
		"console_login = /bin/sh\n";
	struct kmorph_config c;
	char err[128];

	CHECK_EQ(config_parse(text, &c, err, sizeof(err)), 0);
	CHECK_STREQ(c.name, "backup");
	CHECK_EQ(c.cpus.count, 5);
	CHECK_EQ(c.cpus.ids[4], 20);
	CHECK_EQ(c.memory, 4ULL << 30);
	CHECK_STREQ(c.kernel, "/boot/vmlinuz");
	CHECK_STREQ(c.initrd, "/boot/successor.img");
	CHECK_STREQ(c.cmdline, "console=ttyS0 quiet");
	CHECK_EQ(c.probe_interval_ms, 250);
	CHECK_EQ(c.probe_timeouts, 8);
	CHECK_EQ(c.fence_retries, 1);
	CHECK_STREQ(c.dump, "/var/crash/predecessor.raw");
	CHECK_EQ(c.devices.count, 2);
	CHECK_STREQ(c.devices.items[0], "0000:09:00.0");
	CHECK_STREQ(c.devices.items[1], "0000:0a:00.0");
	CHECK_EQ(c.machine_cpus.count, 4);
	CHECK_EQ(c.machine_cpus.ids[3], 3);
	CHECK_STREQ(c.console, "ttyS0");
	CHECK_EQ(c.console_baud, 9600);
	CHECK_STREQ(c.console_login, "/bin/sh");
	config_free(&c);
}

static void empty_value_means_unset(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK_EQ(config_parse("cmdline =\ndump=\n", &c, err, sizeof(err)), 0);
	CHECK(c.cmdline == NULL);
	CHECK(c.dump == NULL);
	config_free(&c);
}

static void unknown_key_is_reported_with_line(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK(config_parse("name = a\nbogus = 1\n", &c, err, sizeof(err)) < 0);
	CHECK(strstr(err, "line 2") != NULL);
	CHECK(strstr(err, "bogus") != NULL);
}

static void bad_value_is_reported_with_line(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK(config_parse("memory = lots\n", &c, err, sizeof(err)) < 0);
	CHECK(strstr(err, "line 1") != NULL);
	CHECK(strstr(err, "memory") != NULL);
}

static void line_without_equals_is_an_error(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK(config_parse("kernel\n", &c, err, sizeof(err)) < 0);
}

static void probe_timeouts_must_be_positive(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK(config_parse("probe_timeouts = 0\n", &c, err, sizeof(err)) < 0);
}

static void load_reads_a_file(void)
{
	char path[] = "/tmp/kmorph-test-config-XXXXXX";
	struct kmorph_config c;
	char err[128];
	int fd = mkstemp(path);

	CHECK(fd >= 0);
	CHECK(write(fd, "name = fromfile\n", 16) == 16);
	close(fd);
	CHECK_EQ(config_load(path, &c, err, sizeof(err)), 0);
	CHECK_STREQ(c.name, "fromfile");
	config_free(&c);
	unlink(path);
	CHECK(config_load("/nonexistent/kmorph.conf", &c, err, sizeof(err)) < 0);
	CHECK(strstr(err, "/nonexistent/kmorph.conf") != NULL);
}

static void parsed_text_is_kept_verbatim(void)
{
	struct kmorph_config c;
	const char *text = "# a comment\ncpus = 1\n\nmemory = 128MB\n";
	char err[128];

	CHECK_EQ(config_parse(text, &c, err, sizeof(err)), 0);
	CHECK_STREQ(c.text, text);
	config_free(&c);
	CHECK(c.text == NULL);
}

static void console_needs_a_login_program(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK_EQ(config_parse("console = ttyS0\n", &c, err, sizeof(err)), -EINVAL);
	CHECK_STREQ(err, "console needs console_login");
	CHECK_EQ(config_parse("console = ttyS0\nconsole_login = /bin/sh\n", &c, err, sizeof(err)), 0);
	config_free(&c);
}

static void modules_is_a_list_for_the_image_builders(void)
{
	struct kmorph_config c;
	char err[128];

	CHECK_EQ(config_parse("modules = mk_transport, usbhid, hid_generic\n", &c, err, sizeof(err)), 0);
	CHECK_EQ(c.modules.count, 3);
	CHECK_STREQ(c.modules.items[0], "mk_transport");
	CHECK_STREQ(c.modules.items[2], "hid_generic");
	config_free(&c);
}

TEST_MAIN({
	RUN(defaults_apply_when_keys_are_absent);
	RUN(parses_every_key_with_comments_and_spacing);
	RUN(empty_value_means_unset);
	RUN(unknown_key_is_reported_with_line);
	RUN(bad_value_is_reported_with_line);
	RUN(line_without_equals_is_an_error);
	RUN(probe_timeouts_must_be_positive);
	RUN(load_reads_a_file);
	RUN(parsed_text_is_kept_verbatim);
	RUN(console_needs_a_login_program);
	RUN(modules_is_a_list_for_the_image_builders);
})

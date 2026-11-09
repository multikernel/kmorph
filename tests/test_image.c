#include <stdio.h>

#include "test.h"
#include "kmorph/config.h"
#include "image.h"

static void describe(const char *text, char *out, size_t len)
{
	struct kmorph_config cfg;
	char err[64];
	FILE *f = fmemopen(out, len, "w");

	CHECK_EQ(config_parse(text, &cfg, err, sizeof(err)), 0);
	image_describe(&cfg, f);
	fclose(f);
	config_free(&cfg);
}

static void status_names_an_image_of_the_operators_own(void)
{
	char line[256];

	describe("initrd = /boot/own.img\nstandby = nginx.service\n", line, sizeof(line));
	CHECK_STREQ(line, "image: /boot/own.img, the operator's own\n");
}

static void status_names_the_standby_units(void)
{
	char line[256];

	describe("standby = nginx.service, haproxy.service\n", line, sizeof(line));
	CHECK(!strncmp(line, "image: standby for nginx.service, haproxy.service, ", 51));
}

TEST_MAIN({
	RUN(status_names_an_image_of_the_operators_own);
	RUN(status_names_the_standby_units);
})

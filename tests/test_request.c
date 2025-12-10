#include "test.h"
#include "kmorph/request.h"

static void status_request_is_recognised(void)
{
	enum request_kind k;

	CHECK_EQ(request_parse("status\n", &k), 0);
	CHECK_EQ(k, REQUEST_STATUS);
	CHECK_EQ(request_parse("status", &k), 0);
	CHECK_EQ(request_parse("handover\n", &k), 0);
	CHECK_EQ(k, REQUEST_HANDOVER);
	CHECK(request_parse("reboot\n", &k) < 0);
	CHECK(request_parse("", &k) < 0);
}

static void status_reply_is_one_line(void)
{
	char buf[128];
	struct status_reply r = {
		.state = "ARMED",
		.predecessor = "alive",
		.last_probe_ms = 12,
		.last_error = 0,
	};

	CHECK(request_format_status(&r, buf, sizeof(buf)) > 0);
	CHECK_STREQ(buf, "ok ARMED predecessor=alive last_probe=12ms error=0\n");
}

static void status_reply_is_parsed_back(void)
{
	struct status_reply r;
	char line[] = "ok TAKEN_OVER predecessor=silent last_probe=1500ms error=-5\n";

	CHECK_EQ(request_parse_status(line, &r), 0);
	CHECK_STREQ(r.state, "TAKEN_OVER");
	CHECK_STREQ(r.predecessor, "silent");
	CHECK_EQ(r.last_probe_ms, 1500);
	CHECK_EQ(r.last_error, -5);
	CHECK(request_parse_status("err something\n", &r) < 0);
}

TEST_MAIN({
	RUN(status_request_is_recognised);
	RUN(status_reply_is_one_line);
	RUN(status_reply_is_parsed_back);
})

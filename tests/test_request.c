#include "test.h"
#include "kmorph/request.h"

static void status_request_is_recognised(void)
{
	enum request_kind k;
	size_t len;

	CHECK_EQ(request_parse("status\n", &k, &len), 0);
	CHECK_EQ(k, REQUEST_STATUS);
	CHECK_EQ(request_parse("status", &k, &len), 0);
	CHECK_EQ(request_parse("handover\n", &k, &len), 0);
	CHECK_EQ(k, REQUEST_HANDOVER);
	CHECK(request_parse("reboot\n", &k, &len) < 0);
	CHECK(request_parse("", &k, &len) < 0);
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

static void handover_reply_round_trips(void)
{
	char buf[128], reason[64];

	CHECK(request_format_handover(NULL, buf, sizeof(buf)) > 0);
	CHECK_STREQ(buf, "ok fencing\n");
	CHECK_EQ(request_parse_handover(buf, reason, sizeof(reason)), 0);

	CHECK(request_format_handover("state=SUSPECT predecessor=silent", buf, sizeof(buf)) > 0);
	CHECK_STREQ(buf, "refused state=SUSPECT predecessor=silent\n");
	CHECK_EQ(request_parse_handover(buf, reason, sizeof(reason)), -EBUSY);
	CHECK_STREQ(reason, "state=SUSPECT predecessor=silent");

	CHECK_EQ(request_parse_handover("err unsupported request\n", reason, sizeof(reason)),
		 -EINVAL);
	CHECK_EQ(request_parse_handover("refused \n", reason, sizeof(reason)), -EINVAL);
}

static void network_request_carries_its_length(void)
{
	enum request_kind k;
	char buf[32];
	size_t len;

	CHECK(request_format_network(1234, buf, sizeof(buf)) > 0);
	CHECK_STREQ(buf, "network 1234\n");
	CHECK_EQ(request_parse(buf, &k, &len), 0);
	CHECK_EQ(k, REQUEST_NETWORK);
	CHECK_EQ(len, 1234);
	CHECK(request_parse("network\n", &k, &len) < 0);
	CHECK(request_parse("network 12x\n", &k, &len) < 0);
	CHECK(request_parse("network 99999999\n", &k, &len) < 0);
}

static void network_reply_round_trips(void)
{
	char buf[64], reason[32];

	CHECK(request_format_network_reply(NULL, buf, sizeof(buf)) > 0);
	CHECK_STREQ(buf, "ok network\n");
	CHECK_EQ(request_parse_network_reply(buf, reason, sizeof(reason)), 0);
	CHECK_EQ(request_parse_handover(buf, reason, sizeof(reason)), -EINVAL);
	CHECK(request_format_network_reply("state=TAKEN_OVER", buf, sizeof(buf)) > 0);
	CHECK_EQ(request_parse_network_reply(buf, reason, sizeof(reason)), -EBUSY);
	CHECK_STREQ(reason, "state=TAKEN_OVER");
}

TEST_MAIN({
	RUN(status_request_is_recognised);
	RUN(status_reply_is_one_line);
	RUN(status_reply_is_parsed_back);
	RUN(handover_reply_round_trips);
	RUN(network_request_carries_its_length);
	RUN(network_reply_round_trips);
})

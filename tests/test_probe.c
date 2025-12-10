#include "test.h"
#include "kmorph/probe.h"

static void reset_and_success_mean_alive(void)
{
	CHECK_EQ(probe_classify(ECONNRESET), PROBE_ALIVE);
	CHECK_EQ(probe_classify(0), PROBE_ALIVE);
}

static void timeout_means_silent(void)
{
	CHECK_EQ(probe_classify(ETIMEDOUT), PROBE_SILENT);
}

static void local_errors_are_not_evidence(void)
{
	CHECK_EQ(probe_classify(ENODEV), PROBE_ERROR);
	CHECK_EQ(probe_classify(EADDRNOTAVAIL), PROBE_ERROR);
	CHECK_EQ(probe_classify(EINVAL), PROBE_ERROR);
}

static void names_are_stable(void)
{
	CHECK_STREQ(probe_result_name(PROBE_ALIVE), "alive");
	CHECK_STREQ(probe_result_name(PROBE_SILENT), "silent");
	CHECK_STREQ(probe_result_name(PROBE_ERROR), "error");
}

TEST_MAIN({
	RUN(reset_and_success_mean_alive);
	RUN(timeout_means_silent);
	RUN(local_errors_are_not_evidence);
	RUN(names_are_stable);
})

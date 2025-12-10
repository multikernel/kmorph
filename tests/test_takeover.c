#include "test.h"
#include "takeover.h"

struct fake {
	char calls[64];
	int fence_fail_times;
	int adopt_fail_once;
	int preserve_fail;
};

static void record(struct fake *f, char c)
{
	size_t n = strlen(f->calls);

	if (n + 1 < sizeof(f->calls))
		f->calls[n] = c;
}

static int fake_fence(void *ctx)
{
	struct fake *f = ctx;

	record(f, 'F');
	if (f->fence_fail_times > 0) {
		f->fence_fail_times--;
		return -ETIMEDOUT;
	}
	return 0;
}

static int fake_adopt(void *ctx)
{
	struct fake *f = ctx;

	record(f, 'A');
	if (f->adopt_fail_once) {
		f->adopt_fail_once = 0;
		return -EIO;
	}
	return 0;
}
static int fake_preserve(void *ctx)
{
	struct fake *f = ctx;

	record(f, 'P');
	return f->preserve_fail ? -EIO : 0;
}
static int fake_reap(void *ctx) { record(ctx, 'R'); return 0; }
static void fake_on_state(struct takeover *t, enum takeover_state s) { (void)t; (void)s; }

static const struct takeover_ops ops = {
	.fence = fake_fence,
	.adopt = fake_adopt,
	.preserve = fake_preserve,
	.reap = fake_reap,
	.on_state = fake_on_state,
};

static void silence_below_threshold_only_suspects(void)
{
	struct fake f = { 0 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 3, 1);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_SUSPECT);
	CHECK_STREQ(f.calls, "");
	takeover_handle(&t, TK_EV_PROBE_ALIVE);
	CHECK_EQ(t.state, TK_ARMED);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_SUSPECT);
	CHECK_STREQ(f.calls, "");
}

static void threshold_runs_the_whole_sequence(void)
{
	struct fake f = { 0 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 2, 1);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_TAKEN_OVER);
	CHECK_STREQ(f.calls, "FAPR");
	CHECK_EQ(t.last_error, 0);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	takeover_handle(&t, TK_EV_PROBE_ALIVE);
	CHECK_STREQ(f.calls, "FAPR");
	CHECK_EQ(t.state, TK_TAKEN_OVER);
}

static void fence_failure_is_retried_on_later_silence(void)
{
	struct fake f = { .fence_fail_times = 1 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 1, 2);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_FENCE_FAILED);
	CHECK_EQ(t.last_error, -ETIMEDOUT);
	CHECK_STREQ(f.calls, "F");
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_TAKEN_OVER);
	CHECK_STREQ(f.calls, "FFAPR");
}

static void fence_retries_are_bounded(void)
{
	struct fake f = { .fence_fail_times = 10 };
	struct takeover t;
	int i;

	takeover_init(&t, &ops, &f, 1, 2);
	for (i = 0; i < 6; i++)
		takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_FENCE_FAILED);
	CHECK_STREQ(f.calls, "FFF");
}

static void alive_after_fence_failure_does_not_rearm(void)
{
	struct fake f = { .fence_fail_times = 1 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 1, 2);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	takeover_handle(&t, TK_EV_PROBE_ALIVE);
	CHECK_EQ(t.state, TK_FENCE_FAILED);
}

static void adopt_failure_resumes_from_fenced(void)
{
	struct fake f = { .adopt_fail_once = 1 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 1, 0);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_FENCED);
	CHECK_EQ(t.last_error, -EIO);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_TAKEN_OVER);
	CHECK_STREQ(f.calls, "FAAPR");
}

static void preserve_failure_keeps_memory_unreaped(void)
{
	struct fake f = { .preserve_fail = 1 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 1, 0);
	takeover_handle(&t, TK_EV_PROBE_SILENT);
	CHECK_EQ(t.state, TK_TAKEN_OVER);
	CHECK_EQ(t.last_error, -EIO);
	CHECK_STREQ(f.calls, "FAP");
}

static void request_event_starts_takeover_directly(void)
{
	struct fake f = { 0 };
	struct takeover t;

	takeover_init(&t, &ops, &f, 5, 0);
	takeover_handle(&t, TK_EV_REQUEST);
	CHECK_EQ(t.state, TK_TAKEN_OVER);
	CHECK_STREQ(f.calls, "FAPR");
}

static void state_names_are_stable(void)
{
	CHECK_STREQ(takeover_state_name(TK_ARMED), "ARMED");
	CHECK_STREQ(takeover_state_name(TK_SUSPECT), "SUSPECT");
	CHECK_STREQ(takeover_state_name(TK_FENCING), "FENCING");
	CHECK_STREQ(takeover_state_name(TK_FENCE_FAILED), "FENCE_FAILED");
	CHECK_STREQ(takeover_state_name(TK_FENCED), "FENCED");
	CHECK_STREQ(takeover_state_name(TK_TAKEN_OVER), "TAKEN_OVER");
}

TEST_MAIN({
	RUN(silence_below_threshold_only_suspects);
	RUN(threshold_runs_the_whole_sequence);
	RUN(fence_failure_is_retried_on_later_silence);
	RUN(fence_retries_are_bounded);
	RUN(alive_after_fence_failure_does_not_rearm);
	RUN(adopt_failure_resumes_from_fenced);
	RUN(preserve_failure_keeps_memory_unreaped);
	RUN(request_event_starts_takeover_directly);
	RUN(state_names_are_stable);
})

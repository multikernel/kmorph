#include <string.h>

#include "takeover.h"

void takeover_init(struct takeover *t, const struct takeover_ops *ops, void *ctx,
		   unsigned int probe_timeouts, unsigned int fence_retries)
{
	memset(t, 0, sizeof(*t));
	t->ops = ops;
	t->ctx = ctx;
	t->probe_timeouts = probe_timeouts;
	t->fence_retries = fence_retries;
}

static void set_state(struct takeover *t, enum takeover_state s)
{
	t->state = s;
	t->ops->on_state(t, s);
}

static int step(struct takeover *t, int (*op)(void *))
{
	t->last_error = op(t->ctx);
	return t->last_error;
}

/* Once fenced, a failed adoption is retried on a later event. */
static void continue_takeover(struct takeover *t)
{
	if (step(t, t->ops->adopt))
		return;
	/* A failed dump leaves the memory unclaimed rather than destroying it. */
	if (!step(t, t->ops->preserve))
		step(t, t->ops->reap);
	set_state(t, TK_TAKEN_OVER);
}

static void begin_takeover(struct takeover *t)
{
	set_state(t, TK_FENCING);
	t->fence_attempts++;
	if (step(t, t->ops->fence)) {
		set_state(t, TK_FENCE_FAILED);
		return;
	}
	set_state(t, TK_FENCED);
	continue_takeover(t);
}

static void handle_watching(struct takeover *t, enum takeover_event ev)
{
	switch (ev) {
	case TK_EV_PROBE_ALIVE:
		t->silent = 0;
		if (t->state != TK_ARMED)
			set_state(t, TK_ARMED);
		break;
	case TK_EV_PROBE_SILENT:
		if (++t->silent >= t->probe_timeouts)
			begin_takeover(t);
		else if (t->state != TK_SUSPECT)
			set_state(t, TK_SUSPECT);
		break;
	case TK_EV_REQUEST:
		begin_takeover(t);
		break;
	}
}

void takeover_handle(struct takeover *t, enum takeover_event ev)
{
	switch (t->state) {
	case TK_ARMED:
	case TK_SUSPECT:
		handle_watching(t, ev);
		break;
	case TK_FENCE_FAILED:
		/* NMIs are idempotent, so a fence may be retried; a bounded number of times. */
		if (ev != TK_EV_PROBE_ALIVE && t->fence_attempts <= t->fence_retries)
			begin_takeover(t);
		break;
	case TK_FENCED:
		continue_takeover(t);
		break;
	case TK_FENCING:
	case TK_TAKEN_OVER:
		break;
	}
}

const char *takeover_state_name(enum takeover_state s)
{
	static const char *const names[] = {
		[TK_ARMED] = "ARMED",
		[TK_SUSPECT] = "SUSPECT",
		[TK_FENCING] = "FENCING",
		[TK_FENCE_FAILED] = "FENCE_FAILED",
		[TK_FENCED] = "FENCED",
		[TK_TAKEN_OVER] = "TAKEN_OVER",
	};

	return names[s];
}

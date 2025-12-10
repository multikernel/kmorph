#ifndef KMORPHD_TAKEOVER_H
#define KMORPHD_TAKEOVER_H

/*
 * The takeover state machine. Pure logic: every effect goes through the
 * ops table, so the sequence is testable without a kernel.
 *
 * ARMED -> SUSPECT -> FENCING -> FENCED -> TAKEN_OVER
 *                        |
 *                        v
 *                   FENCE_FAILED (retried on later silence, bounded)
 */

enum takeover_state {
	TK_ARMED,
	TK_SUSPECT,
	TK_FENCING,
	TK_FENCE_FAILED,
	TK_FENCED,
	TK_TAKEN_OVER,
};

enum takeover_event {
	TK_EV_PROBE_ALIVE,
	TK_EV_PROBE_SILENT,
	TK_EV_REQUEST,		/* a planned handover; unused in v1 */
};

struct takeover;

struct takeover_ops {
	int (*fence)(void *ctx);
	int (*adopt)(void *ctx);
	int (*preserve)(void *ctx);
	int (*reap)(void *ctx);
	void (*on_state)(struct takeover *t, enum takeover_state state);
};

struct takeover {
	enum takeover_state state;
	const struct takeover_ops *ops;
	void *ctx;
	unsigned int probe_timeouts;
	unsigned int fence_retries;
	unsigned int silent;
	unsigned int fence_attempts;
	int last_error;
};

void takeover_init(struct takeover *t, const struct takeover_ops *ops, void *ctx,
		   unsigned int probe_timeouts, unsigned int fence_retries);
void takeover_handle(struct takeover *t, enum takeover_event ev);
const char *takeover_state_name(enum takeover_state s);

#endif

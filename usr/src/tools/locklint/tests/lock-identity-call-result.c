/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Characterize a mutex reached through a pointer-valued member of an object
 * returned by a call.  A balanced loop must retain one mutex identity across
 * its drop and reacquisition, while omitting the reacquisition must remain
 * visible.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct inner_state {
	mutex_t lock;
	int busy;
};

struct outer_state {
	struct inner_state *inner;
};

struct request_state {
	struct outer_state *outer;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void delay(void);
extern struct outer_state *get_outer(struct request_state *);

void balanced_call_result_loop(struct request_state *, int);
void missing_reacquire_loop(struct request_state *, int);

void
balanced_call_result_loop(struct request_state *request, int count)
{
	struct outer_state *outer = get_outer(request);
	int i;

	mutex_enter(&outer->inner->lock);
	for (i = 0; i < count; i++) {
		if (outer->inner->busy) {
			mutex_exit(&outer->inner->lock);
			delay();
			mutex_enter(&outer->inner->lock);
		} else {
			break;
		}
	}
	mutex_exit(&outer->inner->lock);
}

void
missing_reacquire_loop(struct request_state *request, int count)
{
	struct outer_state *outer = get_outer(request);
	int i;

	mutex_enter(&outer->inner->lock);
	for (i = 0; i < count; i++) {
		if (outer->inner->busy) {
			mutex_exit(&outer->inner->lock);
			delay();
		} else {
			break;
		}
	}
	mutex_exit(&outer->inner->lock);
}

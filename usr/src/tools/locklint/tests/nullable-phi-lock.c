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
 * Verify that cleanup reached both before and after pointer initialization
 * retains the identity of a lock acquired through the initialized pointer.
 * Optimization exposes distinct normal-path and nullable cleanup merges.
 * The externally visible check is also called through a wrapper so the
 * duplicate acquisition is reported at its source rather than at both sites.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct nullable_phi_state {
	mutex_t lock;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern struct nullable_phi_state *external_lookup(void *);
extern void cleanup_log(struct nullable_phi_state *);
void check_nullable_cleanup(void *, int, int, int, int);

static struct nullable_phi_state *
lookup_state(void *key)
{
	return (external_lookup(key));
}

void
check_nullable_cleanup(void *key, int fail_before_lookup,
    int use_first_lookup, int fail_before_lock, int fail_while_locked)
{
	struct nullable_phi_state *state = (void *)0;

	if (use_first_lookup) {
		state = lookup_state(key);
	} else {
		if (fail_before_lookup)
			goto fail;
		state = lookup_state(key);
	}
	if (fail_before_lock)
		goto fail;

	mutex_enter(&state->lock);
	if (fail_while_locked)
		goto fail;
	mutex_exit(&state->lock);

	return;

fail:
	cleanup_log(state);
	if (state != (void *)0) {
		mutex_enter(&state->lock);
		mutex_exit(&state->lock);
	}
}

static void
call_nullable_cleanup(void *key)
{
	check_nullable_cleanup(key, 0, 0, 0, 1);
}

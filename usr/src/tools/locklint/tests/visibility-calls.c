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

#define	_NOTE(arg)

typedef int mutex_t;

struct visibility_call_state {
	mutex_t lock;
	int protected;
	int sibling;
	int read_only;
};

_NOTE(MUTEX_PROTECTS_DATA(visibility_call_state::lock,
    visibility_call_state::protected))
_NOTE(MUTEX_PROTECTS_DATA(visibility_call_state::lock,
    visibility_call_state::sibling))
_NOTE(READ_ONLY_DATA(visibility_call_state::read_only))

static void
make_invisible(struct visibility_call_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
}

static void
make_visible(struct visibility_call_state *state)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->protected))
}

static void
wrap_invisible(struct visibility_call_state *state)
{
	make_invisible(state);
}

static void
maybe_invisible(struct visibility_call_state *state, int make_private)
{
	if (make_private)
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
}

static void
always_invisible(struct visibility_call_state *state, int first)
{
	if (first) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	} else {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	}
}

static void
whole_invisible_member_visible(struct visibility_call_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->protected))
}

static void
no_visibility_change(struct visibility_call_state *state)
{
	(void)state;
}

static void
invisible_direct_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	make_invisible(state);
	state->protected = 1;
}

static void
visible_direct_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	make_visible(state);
	state->protected = 1;
}

static void
invisible_transitive_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	wrap_invisible(state);
	state->protected = 1;
}

static void
maybe_invisible_caller(struct visibility_call_state *state, int make_private)
{
	_NOTE(COMPETING_THREADS_NOW)
	maybe_invisible(state, make_private);
	state->protected = 1;
}

static void
maybe_already_invisible_caller(struct visibility_call_state *state,
    int make_private)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	maybe_invisible(state, make_private);
	state->protected = 1;
}

static void
always_invisible_caller(struct visibility_call_state *state, int first)
{
	_NOTE(COMPETING_THREADS_NOW)
	always_invisible(state, first);
	state->protected = 1;
}

static void
overlapping_effect_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	whole_invisible_member_visible(state);
	state->protected = 1;
	state->sibling = 1;
}

static void
unchanged_invisible_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	no_visibility_change(state);
	state->protected = 1;
}

static struct visibility_call_state global_state;

static void
make_global_invisible(void)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(global_state.protected))
}

static void
make_global_visible(void)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(global_state.protected))
}

static void
global_invisible_caller(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	make_global_invisible();
	global_state.protected = 1;
}

static void
global_visible_caller(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(global_state.protected))
	make_global_visible();
	global_state.protected = 1;
}

static void
withdraw_read_only(struct visibility_call_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->read_only))
}

static void
publish_read_only(struct visibility_call_state *state)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->read_only))
}

static void
withdrawn_read_only_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	withdraw_read_only(state);
	state->read_only = 1;
}

static void
published_read_only_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->read_only))
	publish_read_only(state);
	state->read_only = 1;
}

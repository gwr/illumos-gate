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
 * Verify propagation of visibility effects through calls.  Direct, wrapped,
 * recursive, global, and nested-object callees establish or withdraw
 * visibility before protected accesses; conditional callees show that an
 * effect is usable only when it occurs on every path, and overlapping
 * whole-object/member effects test most-specific-state precedence.
 */

#define	_NOTE(arg)

typedef int mutex_t;

struct visibility_call_state {
	mutex_t lock;
	int protected;
	int sibling;
	int read_only;
	struct {
		int protected;
	} nested;
};

_NOTE(MUTEX_PROTECTS_DATA(visibility_call_state::lock,
    visibility_call_state::protected))
_NOTE(MUTEX_PROTECTS_DATA(visibility_call_state::lock,
    visibility_call_state::sibling))
_NOTE(MUTEX_PROTECTS_DATA(visibility_call_state::lock,
    visibility_call_state::nested.protected))
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

static void
maybe_hide_whole(struct visibility_call_state *state, int make_private)
{
	if (make_private)
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
}

static void
narrow_state_survives_broad_effect(struct visibility_call_state *state,
    int make_private)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	maybe_hide_whole(state, make_private);
	state->protected = 1;
}

static void
maybe_show_whole_then_hide_member(struct visibility_call_state *state,
    int publish)
{
	if (publish) {
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	}
}

static void
narrow_effect_overrides_broad_effect(struct visibility_call_state *state,
    int publish)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	maybe_show_whole_then_hide_member(state, publish);
	state->protected = 1;
}

static void
recursive_invisible(struct visibility_call_state *state, int depth)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	if (depth != 0)
		recursive_invisible(state, depth - 1);
}

static void
recursive_invisible_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	recursive_invisible(state, 2);
	state->protected = 1;
}

struct visibility_wrapper {
	int prefix;
	struct visibility_call_state inner;
};

static void
nested_invisible_caller(struct visibility_wrapper *wrapper)
{
	_NOTE(COMPETING_THREADS_NOW)
	make_invisible(&wrapper->inner);
	wrapper->inner.protected = 1;
}

static void
nested_visible_caller(struct visibility_wrapper *wrapper)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(wrapper->inner.protected))
	make_visible(&wrapper->inner);
	wrapper->inner.protected = 1;
}

static void
maybe_hide_nested_member(struct visibility_call_state *state, int hide)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->nested))
	if (hide)
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->nested.protected))
}

static void
nested_summary_merge_caller(struct visibility_call_state *state, int hide)
{
	_NOTE(COMPETING_THREADS_NOW)
	maybe_hide_nested_member(state, hide);
	state->nested.protected = 1;
}

static void
show_whole_hide_nested_leaf(struct visibility_call_state *state)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->nested.protected))
}

static void
three_level_effect_caller(struct visibility_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->nested))
	show_whole_hide_nested_leaf(state);
	state->nested.protected = 1;
}

static void
hide_whole(struct visibility_call_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
}

static void
show_whole(struct visibility_call_state *state)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
}

static void
nested_whole_invisible_caller(struct visibility_wrapper *wrapper)
{
	_NOTE(COMPETING_THREADS_NOW)
	hide_whole(&wrapper->inner);
	wrapper->inner.sibling = 1;
}

static void
nested_whole_visible_caller(struct visibility_wrapper *wrapper)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(wrapper->inner))
	show_whole(&wrapper->inner);
	wrapper->inner.sibling = 1;
}

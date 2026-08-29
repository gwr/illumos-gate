/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Gordon W. Ross
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct visibility_nested {
	int protected;
};

struct visibility_state {
	mutex_t lock;
	int protected;
	int sibling;
	int read_only;
	struct visibility_nested nested;
};

static struct visibility_state visibility_object;

_NOTE(MUTEX_PROTECTS_DATA(visibility_state::lock,
    visibility_state::protected))
_NOTE(MUTEX_PROTECTS_DATA(visibility_state::lock,
    visibility_state::sibling))
_NOTE(MUTEX_PROTECTS_DATA(visibility_state::lock,
    visibility_state::nested.protected))
_NOTE(READ_ONLY_DATA(visibility_state::read_only))

static void
competition_markers(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	visibility_object.protected = value;
	_NOTE(COMPETING_THREADS_NOW)
	visibility_object.protected = value;
}

static void
competition_merge(int value, int no_competition)
{
	if (no_competition) {
		_NOTE(NO_COMPETING_THREADS_NOW)
	} else {
		_NOTE(COMPETING_THREADS_NOW)
	}
	visibility_object.protected = value;
}

static void
visibility_markers(struct visibility_state *state, int value, int invisible)
{
	if (invisible) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
		state->protected = value;
	} else {
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
		state->protected = value;
	}
}

static void
visibility_overrides(struct visibility_state *state, int value)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->protected))
	state->protected = value;
	state->sibling = value;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	state->protected = value;
	state->sibling = value;

	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
	state->protected = value;
	state->sibling = value;
}

static void
visibility_multiple(struct visibility_state *first,
    struct visibility_state *second, int value)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(first->protected,
	    second->protected))
	first->protected = value;
	second->protected = value;
	first->sibling = value;
}

static void
visibility_descendant(struct visibility_state *state, int value)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->nested))
	state->nested.protected = value;
	state->sibling = value;
}

static void
visibility_merge(struct visibility_state *state, int value, int invisible)
{
	if (invisible) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	} else {
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->protected))
	}
	state->protected = value;
}

static void
visibility_merge_agreement(struct visibility_state *state, int value,
    int first_path)
{
	if (first_path) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	} else {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->protected))
	}
	state->protected = value;
}

static void
visibility_invalid(void)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(1))
}

static void
read_only_competition(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	visibility_object.read_only = value;
	_NOTE(COMPETING_THREADS_NOW)
	visibility_object.read_only = value;
}

static void
read_only_visibility(struct visibility_state *state, int value)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->read_only))
	state->read_only = value;
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->read_only))
	state->read_only = value;
}

static void
read_only_withdrawal(struct visibility_state *state, int value)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->read_only))
	state->read_only = value;
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->read_only))
	state->read_only = value;
}

static void
read_only_merge(struct visibility_state *state, int value, int invisible)
{
	if (invisible) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->read_only))
	} else {
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->read_only))
	}
	state->read_only = value;
}

static void
contract_markers(struct visibility_state *first,
    struct visibility_state *second)
{
	_NOTE(ASSUMING_PROTECTED(first->protected, second->read_only))
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
}

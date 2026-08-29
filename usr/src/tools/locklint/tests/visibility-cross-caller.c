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

#include "visibility-cross.h"

static void
formal_invisible_caller(struct visibility_cross_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	visibility_cross_make_invisible(state);
	state->value = 1;
}

static void
formal_visible_caller(struct visibility_cross_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->value))
	visibility_cross_make_visible(state);
	state->value = 1;
}

static void
global_invisible_caller(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	visibility_cross_make_global_invisible();
	visibility_cross_global.value = 1;
}

static void
global_visible_caller(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(visibility_cross_global.value))
	visibility_cross_make_global_visible();
	visibility_cross_global.value = 1;
}

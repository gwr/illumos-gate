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

void
visibility_cross_make_invisible(struct visibility_cross_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->value))
}

void
visibility_cross_make_visible(struct visibility_cross_state *state)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->value))
}

struct visibility_cross_state visibility_cross_global;

void
visibility_cross_make_global_invisible(void)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(visibility_cross_global.value))
}

void
visibility_cross_make_global_visible(void)
{
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(visibility_cross_global.value))
}

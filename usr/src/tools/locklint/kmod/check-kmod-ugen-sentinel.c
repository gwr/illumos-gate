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
 * Supply one deliberate protected-data violation to prove that the ugen
 * comparison exercised lock analysis.  The runner removes exactly this
 * diagnostic before comparing ugen's findings with the OSLL reference.
 */

#define	_NOTE(arg)

typedef int locklint_ugen_sentinel_mutex_t;

struct locklint_ugen_sentinel_state {
	locklint_ugen_sentinel_mutex_t lock;
	int value;
};

static struct locklint_ugen_sentinel_state locklint_ugen_sentinel_state;

_NOTE(MUTEX_PROTECTS_DATA(locklint_ugen_sentinel_state::lock,
    locklint_ugen_sentinel_state::value))

int locklint_ugen_sentinel(void);

int
locklint_ugen_sentinel(void)
{
	_NOTE(COMPETING_THREADS_NOW)

	return (locklint_ugen_sentinel_state.value);
}

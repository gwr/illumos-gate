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
 * Supply one intentional negative protected-data access to prove that a
 * kernel-module comparison exercised lock diagnostics.  Each runner requires
 * exactly this warning and removes it before comparing the module's findings
 * with its OSLL reference.
 */

#define	_NOTE(arg)

typedef int locklint_kmod_sentinel_mutex_t;

struct locklint_kmod_sentinel_state {
	locklint_kmod_sentinel_mutex_t lock;
	int value;
};

static struct locklint_kmod_sentinel_state locklint_kmod_sentinel_state;

_NOTE(MUTEX_PROTECTS_DATA(locklint_kmod_sentinel_state::lock,
    locklint_kmod_sentinel_state::value))

int locklint_kmod_sentinel(void);

int
locklint_kmod_sentinel(void)
{
	_NOTE(COMPETING_THREADS_NOW)

	return (locklint_kmod_sentinel_state.value);
}

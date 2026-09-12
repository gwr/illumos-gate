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
 * Verify an annotation inside a function resolves a visible function-local
 * static instead of a same-named file-scope object.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

static mutex_t file_lock;
static int polledio_inited;
static int inline_state;
static int loop_index;

_NOTE(MUTEX_PROTECTS_DATA(file_lock, polledio_inited))
_NOTE(MUTEX_PROTECTS_DATA(file_lock, inline_state))
_NOTE(MUTEX_PROTECTS_DATA(file_lock, loop_index))

static void
initialize_polled_io(void)
{
	static int polledio_inited;

	_NOTE(SCHEME_PROTECTS_DATA("single-threaded initialization",
	    polledio_inited))
	_NOTE(COMPETING_THREADS_NOW)

	polledio_inited = 1;

	_NOTE(NO_COMPETING_THREADS_NOW)
}

static int
read_file_state(void)
{
	return (polledio_inited);
}

static void
check_nested_scope(void)
{
	static mutex_t local_lock;
	static int shadowed;

	_NOTE(MUTEX_PROTECTS_DATA(local_lock, shadowed))
	{
		static int shadowed;

		_NOTE(SCHEME_PROTECTS_DATA("inner block", shadowed))
		shadowed = 1;
	}
	_NOTE(SCHEME_PROTECTS_DATA("outer block", shadowed))
	shadowed = 1;
}

static inline void
check_inline_scope(void)
{
	static int inline_state;

	_NOTE(SCHEME_PROTECTS_DATA("inline function", inline_state))
	inline_state = 1;
}

static void
check_for_scope(void)
{
	for (int loop_index = 0; loop_index != 1; loop_index++) {
		_NOTE(SCHEME_PROTECTS_DATA("for initializer", loop_index))
		loop_index = 1;
	}
}

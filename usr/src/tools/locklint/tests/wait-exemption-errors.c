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
 * Verify invalid held-during-wait exemption transitions: enabling an
 * exemption for an unheld lock, disabling an inactive exemption, enabling it
 * twice, releasing the lock while exempt, and returning with changed
 * exemption state.
 */

#ifdef __lock_lint
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)

typedef struct kmutex {
	int opaque;
} kmutex_t;
#endif

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);

static kmutex_t exemption_error_lock;

void
exemption_on_unheld(void)
{
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exemption_error_lock))
}

void
exemption_off_inactive(void)
{
	mutex_enter(&exemption_error_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exemption_error_lock))
	mutex_exit(&exemption_error_lock);
}

void
exemption_on_twice(void)
{
	mutex_enter(&exemption_error_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exemption_error_lock))
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exemption_error_lock))
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exemption_error_lock))
	mutex_exit(&exemption_error_lock);
}

void
exemption_active_at_release(void)
{
	mutex_enter(&exemption_error_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exemption_error_lock))
	mutex_exit(&exemption_error_lock);
}

void
exemption_active_on_return(void)
{
	mutex_enter(&exemption_error_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exemption_error_lock))
}

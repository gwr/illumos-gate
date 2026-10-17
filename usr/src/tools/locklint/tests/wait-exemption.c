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
 * Verify scoped exemptions from held-during-wait diagnostics.  Exemptions
 * suppress only the named held lock, cross ordinary calls, end at OFF, and
 * remain conservative when active on only one control-flow path.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)

typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct kcondvar {
	int opaque;
} kcondvar_t;
#endif

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);

static kmutex_t exempt_lock;
static kmutex_t other_lock;
static kmutex_t wait_lock;
static kcondvar_t wait_cv;

/*
 * Keep the actual condition wait in a helper so callers also exercise
 * interprocedural exemption propagation.
 */
static void
wait_in_exempt_helper(void)
{
	mutex_enter(&wait_lock);
	cv_wait(&wait_cv, &wait_lock);
	mutex_exit(&wait_lock);
}

static void
wait_in_conditional_helper(void)
{
	mutex_enter(&wait_lock);
	cv_wait(&wait_cv, &wait_lock);
	mutex_exit(&wait_lock);
}

void
wait_without_exemption(void)
{
	mutex_enter(&exempt_lock);
	mutex_enter(&wait_lock);
	cv_wait(&wait_cv, &wait_lock);
	mutex_exit(&wait_lock);
	mutex_exit(&exempt_lock);
}

void
wait_with_exemption(void)
{
	mutex_enter(&exempt_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exempt_lock))
	wait_in_exempt_helper();
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exempt_lock))
	mutex_exit(&exempt_lock);
}

void
wait_with_exemption_and_other_lock(void)
{
	mutex_enter(&exempt_lock);
	mutex_enter(&other_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exempt_lock))
	mutex_enter(&wait_lock);
	cv_wait(&wait_cv, &wait_lock);
	mutex_exit(&wait_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exempt_lock))
	mutex_exit(&other_lock);
	mutex_exit(&exempt_lock);
}

void
wait_after_exemption(void)
{
	mutex_enter(&exempt_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exempt_lock))
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exempt_lock))
	mutex_enter(&wait_lock);
	cv_wait(&wait_cv, &wait_lock);
	mutex_exit(&wait_lock);
	mutex_exit(&exempt_lock);
}

void
wait_with_conditional_exemption(int exempt)
{
	mutex_enter(&exempt_lock);
	if (exempt) {
		_NOTE(HELD_DURING_WAIT_EXEMPT_ON(exempt_lock))
		wait_in_conditional_helper();
		_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(exempt_lock))
	} else {
		wait_in_conditional_helper();
	}
	mutex_exit(&exempt_lock);
}

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/callb.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/vmsystm.h>
#include <sys/class.h>
#include <sys/debug.h>
#include <sys/thread.h>
// #include <sys/kobj.h>
// #include <sys/ddi.h>	/* for delay() */
// #include <sys/taskq.h>  /* For TASKQ_NAMELEN */

/*
 * The callb mechanism provides generic event scheduling/echoing.
 * A callb function is registered and called on behalf of the event.
 */
typedef struct callb {
	struct callb	*c_next;	/* next in class or on freelist */
	kthread_id_t	c_thread;	/* ptr to caller's thread struct */
	char		c_flag;		/* info about the callb state */
	uchar_t		c_class;	/* this callb's class */
	kcondvar_t	c_done_cv;	/* signal callb completion */
	boolean_t	(*c_func)();	/* cb function: returns true if ok */
	void		*c_arg;		/* arg to c_func */
	char		c_name[CB_MAXNAME+1]; /* debug:max func name length */
} callb_t;

/*
 * callb c_flag bitmap definitions
 */
#define	CALLB_FREE		0x0
#define	CALLB_TAKEN		0x1
#define	CALLB_EXECUTING		0x2

static kmutex_t	callb_safe_mutex;
callb_cpr_t	callb_cprinfo_safe = {
	&callb_safe_mutex, CALLB_CPR_ALWAYS_SAFE, 0, 0, 0 };

void
callb_init()
{
	mutex_init(&callb_safe_mutex, NULL, MUTEX_DEFAULT, NULL);
}
#pragma init(callb_init)

boolean_t
callb_generic_cpr(void *arg, int code)
{
	return (B_TRUE);
}

boolean_t
callb_generic_cpr_safe(void *arg, int code)
{
	return (B_TRUE);
}

static callb_id_t
callb_add_common(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name, kthread_id_t t)
{
	callb_t *cp;

	ASSERT(class < NCBCLASS);

	cp = (callb_t *)kmem_zalloc(sizeof (callb_t), KM_SLEEP);

	cp->c_thread = t;
	cp->c_func = func;
	cp->c_arg = arg;
	cp->c_class = (uchar_t)class;
	cp->c_flag = CALLB_TAKEN;
	(void) strncpy(cp->c_name, name, CB_MAXNAME);
	cp->c_name[CB_MAXNAME] = '\0';

	return ((callb_id_t)cp);
}

callb_id_t
callb_add(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name)
{
	return (callb_add_common(func, arg, class, name, curthread));
}

callb_id_t
callb_add_thread(boolean_t (*func)(void *arg, int code),
    void *arg, int class, char *name, kthread_id_t t)
{
	return (callb_add_common(func, arg, class, name, t));
}

int
callb_delete(callb_id_t id)
{
	callb_t *cp = (callb_t *)id;

	ASSERT(cp->c_flag == CALLB_TAKEN);
	kmem_free(cp, sizeof (callb_t));

	return (0);
}

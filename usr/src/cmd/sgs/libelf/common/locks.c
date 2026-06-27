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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Private lock/unlock helpers. See decl.h
 */

#include <stdlib.h>
#include <errno.h>
#include <libelf.h>
#include "decl.h"

/*
 * This small portability concession is for people who want
 * the ability to cross-build this library on non-illumos.
 */
#ifdef __sun
#include <upanic.h>
#else /* __sun */
static void
upanic(const char *msg, size_t len)
{
	abort();
}
#endif /* __sun */

int _elf_lock_panic_err = 0;
const char *_elf_lock_panic_msg = NULL;
void *_elf_lock_panic_obj = NULL;

/*
 * See the inline functions in decd.h
 * Called if taking a lock fails.
 */
void
_elf_lock_panic(void *obj, int err, const char *msg)
{
	_elf_lock_panic_msg = msg;
	upanic("_elf_lock_panic", 16);
}

/*
 * See the inline functions in decd.h
 * Called if releasing a lock fails.
 * Does nothing; just for debugging.
 */
void
_elf_unlock_warn(void *obj, int err, const char *msg)
{
	(void)obj;
	(void)err;
	(void)msg;
}

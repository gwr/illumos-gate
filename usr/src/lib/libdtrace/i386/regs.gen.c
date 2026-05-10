/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file and it's companion *.d.in are processed by make to
 * create a *.d library file.  The *.d.in file is a template that
 * needs substitutions.  This file creates the dictionary of what
 * names may be substituted.  See Makefile.com for details.
 */

#include <sys/regset.h>

/* This used to be processed by "sed". */
#define	SED_REPLACE(x)		XYZZY_BEGIN #x = x XYZZY_END
#define	SED_REPLACE64(x)	XYZZY_BEGIN #x = x XYZZY_END

SED_REPLACE(GS)
SED_REPLACE(FS)
SED_REPLACE(ES)
SED_REPLACE(DS)
SED_REPLACE(EDI)
SED_REPLACE(ESI)
SED_REPLACE(EBP)
SED_REPLACE(ESP)
SED_REPLACE(EBX)
SED_REPLACE(EDX)
SED_REPLACE(ECX)
SED_REPLACE(EAX)
SED_REPLACE(TRAPNO)
SED_REPLACE(ERR)
SED_REPLACE(EIP)
SED_REPLACE(CS)
SED_REPLACE(EFL)
SED_REPLACE(UESP)
SED_REPLACE(SS)

SED_REPLACE64(REG_RSP)
SED_REPLACE64(REG_RFL)
SED_REPLACE64(REG_RIP)
SED_REPLACE64(REG_RAX)
SED_REPLACE64(REG_RCX)
SED_REPLACE64(REG_RDX)
SED_REPLACE64(REG_RBX)
SED_REPLACE64(REG_RBP)
SED_REPLACE64(REG_RSI)
SED_REPLACE64(REG_RDI)
SED_REPLACE64(REG_R8)
SED_REPLACE64(REG_R9)
SED_REPLACE64(REG_R10)
SED_REPLACE64(REG_R11)
SED_REPLACE64(REG_R12)
SED_REPLACE64(REG_R13)
SED_REPLACE64(REG_R14)
SED_REPLACE64(REG_R15)

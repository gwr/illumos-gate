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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2015 PALO, Richard.  All rights reserved.
 */

#ifndef	_SYS_CTYPE_H
#define	_SYS_CTYPE_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ISDIGIT(_c) \
	((_c) >= '0' && (_c) <= '9')

#define	ISXDIGIT(_c) \
	(ISDIGIT(_c) || \
	((_c) >= 'a' && (_c) <= 'f') || \
	((_c) >= 'A' && (_c) <= 'F'))

#define	ISLOWER(_c) \
	((_c) >= 'a' && (_c) <= 'z')

#define	ISUPPER(_c) \
	((_c) >= 'A' && (_c) <= 'Z')

#define	ISALPHA(_c) \
	(ISUPPER(_c) || \
	ISLOWER(_c))

#define	ISALNUM(_c) \
	(ISALPHA(_c) || \
	ISDIGIT(_c))

#define	ISPRINT(_c) \
	((_c) >= ' ' && (_c) <= '~')

#define	ISSPACE(_c) \
	((_c) == ' ' || \
	(_c) == '\t' || \
	(_c) == '\r' || \
	(_c) == '\n')

#ifdef __lint
/* Hide the inlines from lint */

extern boolean_t isdigit(char);
extern boolean_t isxdigit(char);
extern boolean_t islower(char);
extern boolean_t isupper(char);
extern boolean_t isalpha(char);
extern boolean_t isalnum(char);
extern boolean_t isprint(char);
extern boolean_t isspace(char);

#else	/* __lint */

extern __GNU_INLINE boolean_t
isdigit(char c)
{
	return (ISDIGIT(c));
}

extern __GNU_INLINE boolean_t
isxdigit(char c)
{
	return (ISXDIGIT(c));
}

extern __GNU_INLINE boolean_t
islower(char c)
{
	return (ISLOWER(c));
}

extern __GNU_INLINE boolean_t
isupper(char c)
{
	return (ISUPPER(c));
}

extern __GNU_INLINE boolean_t
isalpha(char c)
{
	return (ISALPHA(c));
}

extern __GNU_INLINE boolean_t
isalnum(char c)
{
	return (ISALNUM(c));
}

extern __GNU_INLINE boolean_t
isprint(char c)
{
	return (ISPRINT(c));
}

extern __GNU_INLINE boolean_t
isspace(char c)
{
	return (ISSPACE(c));
}

#endif	/* __lint */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CTYPE_H */

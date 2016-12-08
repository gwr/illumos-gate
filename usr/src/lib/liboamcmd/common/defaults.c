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
 * Copyright 2016 Gordon W. Ross
 */

/*
 * Get values for things that were historically constants in userdefs.h
 * i.e. DEFRID, DEFSHL
 */

const char *
_userdefs_str(void)
{
#if 0	/* todo */
	static char defshl[MAXPATHLEN];
	void	*defp;

	if (defshl[0] != '\0')
		return (defshl);

	defp = defopen_r(...);
#endif
	return ("fubar");
}

int
_userdefs_int(void)
{
#if 0 	/* todo */
	static int defrid = 0;
	int	val;
	void	*defp;

	if (defrid != 0)
		return (defrid);

	if ((defp = defopen_r(PWADMIN)) == NULL) {
		val = defvalue;
	} else {
		val = def_getuint(name, defvalue, defp);
		defclose_r(defp);
	}
#endif

	return (99);
}

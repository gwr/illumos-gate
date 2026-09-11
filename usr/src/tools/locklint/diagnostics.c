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
 * Emit locklint diagnostics with stable identifiers.  Centralizing emission
 * provides the policy boundary for later suppression and reporting controls.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "diagnostics.h"

static const char *const diagnostic_names[LOCKLINT_DIAG_COUNT] = {
	[LOCKLINT_DIAG_AMBIGUOUS_DIRECT_CALL] = "ambiguous-direct-call",
	[LOCKLINT_DIAG_ASSERTED_LOCK_REQUIREMENT] =
	    "asserted-lock-requirement",
	[LOCKLINT_DIAG_COMPETITION_MAYBE_UNDERFLOW] =
	    "competition-maybe-underflow",
	[LOCKLINT_DIAG_COMPETITION_UNDERFLOW] = "competition-underflow",
	[LOCKLINT_DIAG_CONDITIONAL_ASSERTED_LOCK_REQUIREMENT] =
	    "conditional-asserted-lock-requirement",
	[LOCKLINT_DIAG_CONDITIONAL_PROTECTION] = "conditional-protection",
	[LOCKLINT_DIAG_DECLARED_COMPETITION_EFFECT] =
	    "declared-competition-effect",
	[LOCKLINT_DIAG_DECLARED_LOCK_EFFECT] = "declared-lock-effect",
	[LOCKLINT_DIAG_DECLARED_ORDER] = "declared-order",
	[LOCKLINT_DIAG_DECLARED_ORDER_CYCLE] = "declared-order-cycle",
	[LOCKLINT_DIAG_DECLARED_ORDER_POSSIBLE] = "declared-order-possible",
	[LOCKLINT_DIAG_INVALID_ASSUMING_PROTECTED] =
	    "invalid-assuming-protected",
	[LOCKLINT_DIAG_LOCK_ALREADY_HELD] = "lock-already-held",
	[LOCKLINT_DIAG_LOCK_HELD_ON_RETURN] = "lock-held-on-return",
	[LOCKLINT_DIAG_LOCK_MAYBE_ALREADY_HELD] =
	    "lock-maybe-already-held",
	[LOCKLINT_DIAG_LOCK_MAYBE_HELD_ON_RETURN] =
	    "lock-maybe-held-on-return",
	[LOCKLINT_DIAG_LOCK_MAYBE_NOT_HELD] = "lock-maybe-not-held",
	[LOCKLINT_DIAG_LOCK_MAYBE_NOT_READ_HELD] =
	    "lock-maybe-not-read-held",
	[LOCKLINT_DIAG_LOCK_MAYBE_NOT_WRITE_HELD] =
	    "lock-maybe-not-write-held",
	[LOCKLINT_DIAG_LOCK_NOT_HELD] = "lock-not-held",
	[LOCKLINT_DIAG_LOCK_NOT_READ_HELD] = "lock-not-read-held",
	[LOCKLINT_DIAG_LOCK_NOT_WRITE_HELD] = "lock-not-write-held",
	[LOCKLINT_DIAG_OBSERVED_DEADLOCK] = "observed-deadlock",
	[LOCKLINT_DIAG_READ_ONLY_MAYBE_VISIBLE] =
	    "read-only-maybe-visible",
	[LOCKLINT_DIAG_READ_ONLY_VISIBLE] = "read-only-visible",
	[LOCKLINT_DIAG_UNPROTECTED_ACCESS] = "unprotected-access",
	[LOCKLINT_DIAG_VISIBILITY_NO_OBJECT] = "visibility-no-object"
};

/*
 * Format first so Sparse's warning interface remains the single owner of
 * source-position rendering and warning accounting.
 */
void
locklint_warning(enum locklint_diagnostic diagnostic, struct position pos,
    const char *format, ...)
{
	const char *name;
	char *message;
	va_list ap;
	va_list copy;
	int length;

	if (diagnostic < 0 || diagnostic >= LOCKLINT_DIAG_COUNT ||
	    diagnostic_names[diagnostic] == NULL)
		die("invalid locklint diagnostic identifier");
	name = diagnostic_names[diagnostic];
	va_start(ap, format);
	va_copy(copy, ap);
	length = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (length < 0)
		die("cannot format locklint diagnostic");
	message = malloc((size_t)length + 1);
	if (message == NULL)
		die("out of memory formatting locklint diagnostic");
	(void) vsnprintf(message, (size_t)length + 1, format, ap);
	va_end(ap);
	warning(pos, "locklint: %s [%s]", message, name);
	free(message);
}

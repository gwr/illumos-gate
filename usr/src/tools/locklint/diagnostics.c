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
 * Emit locklint diagnostics with stable identifiers and Sparse source
 * positions, independently of Sparse's parser-warning controls.
 */

#include <stdarg.h>
#include <stdio.h>

#include "token.h"
#include "diagnostics.h"

static const char *program_name = "locklint";

static const char *const diagnostic_names[LOCKLINT_DIAG_COUNT] = {
	[LOCKLINT_DIAG_AMBIGUOUS_DIRECT_CALL] = "ambiguous-direct-call",
	[LOCKLINT_DIAG_ASSERTED_COMPETITION_REQUIREMENT] =
	    "asserted-competition-requirement",
	[LOCKLINT_DIAG_ASSERTED_LOCK_REQUIREMENT] =
	    "asserted-lock-requirement",
	[LOCKLINT_DIAG_COMPETITION_MAYBE_UNDERFLOW] =
	    "competition-maybe-underflow",
	[LOCKLINT_DIAG_COMPETITION_UNDERFLOW] = "competition-underflow",
	[LOCKLINT_DIAG_CONDITIONAL_ASSERTED_COMPETITION_REQUIREMENT] =
	    "conditional-asserted-competition-requirement",
	[LOCKLINT_DIAG_CONDITIONAL_ASSERTED_LOCK_REQUIREMENT] =
	    "conditional-asserted-lock-requirement",
	[LOCKLINT_DIAG_CONDITIONAL_PROTECTION] = "conditional-protection",
	[LOCKLINT_DIAG_COVER_MAYBE_RELEASED_WHILE_COVERED] =
	    "cover-maybe-released-while-covered",
	[LOCKLINT_DIAG_COVER_RELEASED_WHILE_COVERED] =
	    "cover-released-while-covered",
	[LOCKLINT_DIAG_COVERED_LOCK_MAYBE_WITHOUT_COVER] =
	    "covered-lock-maybe-without-cover",
	[LOCKLINT_DIAG_COVERED_LOCK_WITHOUT_COVER] =
	    "covered-lock-without-cover",
	[LOCKLINT_DIAG_DECLARED_COMPETITION_EFFECT] =
	    "declared-competition-effect",
	[LOCKLINT_DIAG_DECLARED_LOCK_EFFECT] = "declared-lock-effect",
	[LOCKLINT_DIAG_DECLARED_ORDER] = "declared-order",
	[LOCKLINT_DIAG_DECLARED_ORDER_CYCLE] = "declared-order-cycle",
	[LOCKLINT_DIAG_DECLARED_ORDER_POSSIBLE] = "declared-order-possible",
	[LOCKLINT_DIAG_FUNCTION_CONTRACT_MISMATCH] =
	    "function-contract-mismatch",
	[LOCKLINT_DIAG_INVALID_ASSUMING_PROTECTED] =
	    "invalid-assuming-protected",
	[LOCKLINT_DIAG_LOCK_ALREADY_HELD] = "lock-already-held",
	[LOCKLINT_DIAG_LOCK_HELD_DURING_WAIT] = "lock-held-during-wait",
	[LOCKLINT_DIAG_LOCK_HELD_ON_RETURN] = "lock-held-on-return",
	[LOCKLINT_DIAG_LOCK_MAYBE_ALREADY_HELD] =
	    "lock-maybe-already-held",
	[LOCKLINT_DIAG_LOCK_MAYBE_HELD_DURING_WAIT] =
	    "lock-maybe-held-during-wait",
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
	[LOCKLINT_DIAG_TYPE_NAME_LAYOUT] = "type-name-layout",
	[LOCKLINT_DIAG_UNANALYZED_CALLBACK] = "unanalyzed-callback",
	[LOCKLINT_DIAG_UNMODELED_INDIRECT_CALL] =
	    "unmodeled-indirect-call",
	[LOCKLINT_DIAG_UNPROTECTED_ACCESS] = "unprotected-access",
	[LOCKLINT_DIAG_VISIBILITY_NO_OBJECT] = "visibility-no-object"
};

void
diagnostics_init(const char *name)
{
	program_name = name;
}

/*
 * Render a source message directly so parser warning limits and warning-to-
 * error conversion cannot alter locklint results.
 */
static void
locklint_vmessage(struct position pos, const char *type, const char *format,
    va_list ap)
{
	if (pos.type == TOKEN_BAD)
		return;
	(void) fflush(stdout);
	(void) fprintf(stderr, "%s: %s:%u:%u: %s", program_name,
	    stream_name(pos.stream), pos.line, pos.pos, type);
	(void) vfprintf(stderr, format, ap);
	(void) fputc('\n', stderr);
}

void
locklint_warning(enum locklint_diagnostic diagnostic, struct position pos,
    const char *format, ...)
{
	va_list ap;

	if (diagnostic < 0 || diagnostic >= LOCKLINT_DIAG_COUNT ||
	    diagnostic_names[diagnostic] == NULL)
		die("invalid locklint diagnostic identifier");
	if (pos.type == TOKEN_BAD)
		return;
	(void) fflush(stdout);
	(void) fprintf(stderr, "%s: %s:%u:%u: warning: ",
	    program_name, stream_name(pos.stream), pos.line, pos.pos);
	va_start(ap, format);
	(void) vfprintf(stderr, format, ap);
	va_end(ap);
	(void) fprintf(stderr, " [%s]\n", diagnostic_names[diagnostic]);
}

void
locklint_info(struct position pos, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	locklint_vmessage(pos, "", format, ap);
	va_end(ap);
}

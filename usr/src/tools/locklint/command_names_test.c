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
 * Verify expansion of the grouped data-name grammar independently of command
 * semantics and program-name resolution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_names.h"

static int failures;

static void
check_names(const char *label, size_t argc, const char *const *argv,
    size_t expected_count, const char *const *expected)
{
	struct command_name_list names = { 0 };
	size_t i;

	if (!command_names_expand(argc, argv, &names)) {
		(void) fprintf(stderr, "%s: unexpected error: %s\n", label,
		    names.error);
		failures++;
		command_names_free(&names);
		return;
	}
	if (names.count != expected_count) {
		(void) fprintf(stderr, "%s: expected %zu names; got %zu\n",
		    label, expected_count, names.count);
		failures++;
	}
	for (i = 0; i < names.count && i < expected_count; i++) {
		if (strcmp(names.names[i], expected[i]) == 0)
			continue;
		(void) fprintf(stderr, "%s: name %zu: expected '%s'; got '%s'\n",
		    label, i + 1, expected[i], names.names[i]);
		failures++;
	}
	command_names_free(&names);
}

static void
check_error(const char *label, size_t argc, const char *const *argv,
    const char *expected)
{
	struct command_name_list names = { 0 };

	if (command_names_expand(argc, argv, &names)) {
		(void) fprintf(stderr, "%s: expected error; expansion succeeded\n",
		    label);
		failures++;
	} else if (names.error == NULL ||
	    strstr(names.error, expected) == NULL) {
		(void) fprintf(stderr, "%s: expected error containing '%s'; "
		    "got '%s'\n", label, expected,
		    names.error != NULL ? names.error : "(null)");
		failures++;
	}
	command_names_free(&names);
}

int
main(void)
{
	static const char *const ordinary[] = {
		"state::first", "object.second"
	};
	static const char *const ordinary_expected[] = {
		"state::first", "object.second"
	};
	static const char *const grouped[] = {
		"state::{", "first,", "nested.{", "one", "two", "}",
		"direct_{", "low", "high", "}", "}", "object.{", "left",
		"right", "}", "global_{", "a", "b", "}"
	};
	static const char *const grouped_expected[] = {
		"state::first",
		"state::nested.one",
		"state::nested.two",
		"state::direct_low",
		"state::direct_high",
		"object.left",
		"object.right",
		"global_a",
		"global_b"
	};
	static const char *const empty[] = {
		"state::{", "}"
	};
	static const char *const unclosed[] = {
		"state::{", "first"
	};
	static const char *const bad_component[] = {
		"state::{", ".first", "}"
	};

	check_names("ordinary names", 2, ordinary, 2, ordinary_expected);
	check_names("grouped names",
	    sizeof (grouped) / sizeof (grouped[0]), grouped,
	    sizeof (grouped_expected) / sizeof (grouped_expected[0]),
	    grouped_expected);
	check_error("empty group", 2, empty, "empty");
	check_error("unclosed group", 2, unclosed, "expected '}'");
	check_error("bad component", 3, bad_component,
	    "expected data-name component");

	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

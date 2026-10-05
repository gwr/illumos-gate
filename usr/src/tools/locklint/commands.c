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
 * Implement locklint command semantics.  The command-file interface is wired
 * before individual commands acquire semantics, so recognized commands fail
 * explicitly rather than appearing to configure an analysis.
 */

#include <ctype.h>
#include <string.h>

#include "annotations.h"
#include "callgraph.h"
#include "command_parse.h"
#include "lib.h"
#include "symbol.h"
#include "type.h"

static int
not_implemented(const char *command)
{
	return (command_parse_error("%s command is not implemented", command));
}

struct declare_options {
	bool external_entry_set;
	bool external_entry;
};

static int
parse_boolean_option(const char *option, const char *name, bool *value)
{
	const char *text;
	size_t length = strlen(name);

	if (strncmp(option, name, length) != 0 || option[length] != '=')
		return (0);
	text = option + length + 1;
	if (strcmp(text, "true") == 0) {
		*value = true;
		return (1);
	}
	if (strcmp(text, "false") == 0) {
		*value = false;
		return (1);
	}
	return (command_parse_error(
	    "invalid Boolean value '%s' for option '%s'", text, name));
}

static int
declare_external_entry(const struct declare_options *options,
    const char *name)
{
	enum callgraph_declare_result result;

	result = callgraph_declare_external_entry(name, options->external_entry,
	    command_parse_path(), command_parse_line());
	switch (result) {
	case CALLGRAPH_DECLARE_OK:
		return (0);
	case CALLGRAPH_DECLARE_UNRESOLVED:
		return (command_parse_error("unresolved function name '%s'",
		    name));
	case CALLGRAPH_DECLARE_AMBIGUOUS:
		return (command_parse_error("ambiguous function name '%s'",
		    name));
	case CALLGRAPH_DECLARE_CONFLICT:
		return (command_parse_error("conflicting value for option "
		    "'--external-entry' on function '%s'", name));
	default:
		return (command_parse_error(
		    "internal error resolving function name '%s'", name));
	}
}

/*
 * Parse the options-first declaration form.  All options are validated before
 * applying the property set to any name.
 */
static int
declare_options(int argc, char **argv)
{
	struct declare_options options = { 0 };
	int first_name;
	int i;

	for (first_name = 0; first_name < argc; first_name++) {
		bool value;
		int result;

		if (strncmp(argv[first_name], "--", 2) != 0)
			break;
		result = parse_boolean_option(argv[first_name],
		    "--external-entry", &value);
		if (result < 0)
			return (-1);
		if (result == 0) {
			return (command_parse_error(
			    "unknown declaration option '%s'",
			    argv[first_name]));
		}
		if (options.external_entry_set &&
		    options.external_entry != value) {
			return (command_parse_error(
			    "conflicting values for option '--external-entry'"));
		}
		options.external_entry_set = true;
		options.external_entry = value;
	}
	if (first_name == argc)
		return (command_parse_error("declare requires at least one name"));
	for (i = first_name; i < argc; i++) {
		if (strncmp(argv[i], "--", 2) == 0) {
			return (command_parse_error(
			    "declaration options must precede names"));
		}
	}
	for (i = first_name; i < argc; i++) {
		if (declare_external_entry(&options, argv[i]) != 0)
			return (-1);
	}
	return (0);
}

static int
declare_entry(int argc, char **argv)
{
	enum callgraph_declare_result result;

	if (argc != 3 ||
	    strcmp(argv[1], "no-competing-threads") != 0) {
		return (command_parse_error("declare entry requires "
		    "'no-competing-threads' and one function name"));
	}
	result = callgraph_declare_entry_no_competing_threads(argv[2],
	    command_parse_path(), command_parse_line());
	switch (result) {
	case CALLGRAPH_DECLARE_OK:
		return (0);
	case CALLGRAPH_DECLARE_UNRESOLVED:
		return (command_parse_error("unresolved function name '%s'",
		    argv[2]));
	case CALLGRAPH_DECLARE_AMBIGUOUS:
		return (command_parse_error("ambiguous function name '%s'",
		    argv[2]));
	default:
		return (command_parse_error(
		    "internal error resolving function name '%s'", argv[2]));
	}
}

static int
declare_targets(int argc, char **argv)
{
	enum callgraph_declare_result result;
	const char *problem;

	if (argc < 3) {
		return (command_parse_error("declare targets requires one "
		    "member and at least one function name"));
	}
	result = callgraph_declare_targets(argv[1], (size_t)(argc - 2),
	    &argv[2], command_parse_path(), command_parse_line(), &problem);
	switch (result) {
	case CALLGRAPH_DECLARE_OK:
		return (0);
	case CALLGRAPH_DECLARE_INVALID_NAME:
		return (command_parse_error(
		    "invalid function-pointer member name '%s'", argv[1]));
	case CALLGRAPH_DECLARE_UNRESOLVED:
		if (problem == argv[1]) {
			return (command_parse_error(
			    "unresolved function-pointer member '%s'",
			    argv[1]));
		}
		return (command_parse_error("unresolved function name '%s'",
		    problem));
	case CALLGRAPH_DECLARE_AMBIGUOUS:
		if (problem == argv[1]) {
			return (command_parse_error(
			    "ambiguous function-pointer member '%s'",
			    argv[1]));
		}
		return (command_parse_error("ambiguous function name '%s'",
		    problem));
	case CALLGRAPH_DECLARE_NOT_FUNCTION_POINTER:
		return (command_parse_error(
		    "member '%s' is not a function pointer", argv[1]));
	case CALLGRAPH_DECLARE_INCOMPATIBLE_TYPE:
		return (command_parse_error(
		    "function '%s' has incompatible type for member '%s'",
		    problem, argv[1]));
	case CALLGRAPH_DECLARE_INCONSISTENT_TYPE:
		return (command_parse_error("inconsistently defined type in "
		    "function-pointer member '%s'", argv[1]));
	case CALLGRAPH_DECLARE_CONFLICT:
		return (command_parse_error("member '%s' has both declared "
		    "targets and a no-lock-effects contract", argv[1]));
	default:
		return (command_parse_error(
		    "internal error resolving declared targets for '%s'",
		    argv[1]));
	}
}

static int
declare_contract(int argc, char **argv)
{
	enum callgraph_declare_result result;
	const char *problem;

	if (argc != 3) {
		return (command_parse_error("declare contract requires one "
		    "member and one contract"));
	}
	if (strcmp(argv[2], "no-lock-effects") == 0) {
		problem = argv[1];
		result = callgraph_declare_no_lock_contract(argv[1],
		    command_parse_path(), command_parse_line());
	} else {
		result = callgraph_declare_representative_contract(argv[1],
		    argv[2], command_parse_path(), command_parse_line(),
		    &problem);
	}
	switch (result) {
	case CALLGRAPH_DECLARE_OK:
		return (0);
	case CALLGRAPH_DECLARE_INVALID_NAME:
		return (command_parse_error(
		    "invalid function-pointer member name '%s'", argv[1]));
	case CALLGRAPH_DECLARE_UNRESOLVED:
		if (problem == argv[1]) {
			return (command_parse_error(
			    "unresolved function-pointer member '%s'",
			    argv[1]));
		}
		return (command_parse_error("unresolved function name '%s'",
		    problem));
	case CALLGRAPH_DECLARE_AMBIGUOUS:
		if (problem == argv[1]) {
			return (command_parse_error(
			    "ambiguous function-pointer member '%s'",
			    argv[1]));
		}
		return (command_parse_error("ambiguous function name '%s'",
		    problem));
	case CALLGRAPH_DECLARE_NOT_FUNCTION_POINTER:
		return (command_parse_error(
		    "member '%s' is not a function pointer", argv[1]));
	case CALLGRAPH_DECLARE_INCONSISTENT_TYPE:
		return (command_parse_error("inconsistently defined type in "
		    "function-pointer member '%s'", argv[1]));
	case CALLGRAPH_DECLARE_CONFLICT:
		return (command_parse_error(
		    "member '%s' has conflicting contract declarations",
		    argv[1]));
	case CALLGRAPH_DECLARE_INCOMPATIBLE_TYPE:
		return (command_parse_error(
		    "function '%s' has incompatible type for member '%s'",
		    problem, argv[1]));
	default:
		return (command_parse_error(
		    "internal error resolving declared contract for '%s'",
		    argv[1]));
	}
}

static int
declare_lock_order(int argc, char **argv)
{
	enum locklint_command_result result;
	const char *problem = NULL;

	if (argc < 3) {
		return (command_parse_error(
		    "declare lock-order requires at least two lock names"));
	}
	result = locklint_declare_lock_order((size_t)(argc - 1),
	    (const char *const *)&argv[1], &problem, command_parse_path(),
	    command_parse_line());
	switch (result) {
	case LOCKLINT_COMMAND_OK:
		return (0);
	case LOCKLINT_COMMAND_INVALID_NAME:
		return (command_parse_error("invalid lock name '%s'", problem));
	case LOCKLINT_COMMAND_UNRESOLVED_NAME:
		return (command_parse_error("unresolved lock name '%s'",
		    problem));
	case LOCKLINT_COMMAND_INCONSISTENT_TYPE:
		return (command_parse_error(
		    "inconsistently defined type in lock name '%s'", problem));
	default:
		return (command_parse_error(
		    "internal error resolving lock name '%s'", problem));
	}
}

static int
declare_mutex_protection(int argc, char **argv)
{
	struct locklint_command_origin origin = { 0 };
	enum locklint_command_result result;
	const char *problem = NULL;

	if (argc < 3) {
		return (command_parse_error("declare mutex-protects-data "
		    "requires one lock and at least one data name"));
	}
	result = locklint_declare_mutex_protection(argv[1],
	    (size_t)(argc - 2), (const char *const *)&argv[2], &problem,
	    &origin, command_parse_path(), command_parse_line());
	switch (result) {
	case LOCKLINT_COMMAND_OK:
		return (0);
	case LOCKLINT_COMMAND_INVALID_NAME:
		return (command_parse_error("invalid %s name '%s'",
		    problem == argv[1] ? "lock" : "data", problem));
	case LOCKLINT_COMMAND_UNRESOLVED_NAME:
		return (command_parse_error("unresolved %s name '%s'",
		    problem == argv[1] ? "lock" : "data", problem));
	case LOCKLINT_COMMAND_INCONSISTENT_TYPE:
		return (command_parse_error("inconsistently defined type in "
		    "%s name '%s'", problem == argv[1] ? "lock" : "data",
		    problem));
	case LOCKLINT_COMMAND_CONFLICT:
		if (origin.column != 0) {
			return (command_parse_error("conflicting mutex protector "
			    "for data name '%s' (previous declaration at "
			    "%s:%lu:%lu)", problem, origin.file, origin.line,
			    origin.column));
		}
		return (command_parse_error("conflicting mutex protector for "
		    "data name '%s' (previous declaration at %s:%lu)",
		    problem, origin.file, origin.line));
	case LOCKLINT_COMMAND_SCOPE_MISMATCH:
		return (command_parse_error("lock and data names must both be "
		    "object-specific or type-member"));
	case LOCKLINT_COMMAND_OWNER_MISMATCH:
		return (command_parse_error("data name '%s' has a different "
		    "owning type from lock name '%s'", problem, argv[1]));
	default:
		return (command_parse_error(
		    "internal error resolving mutex protection for '%s'",
		    problem));
	}
}

int
cmd_assert(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return (not_implemented("assert"));
}

int
cmd_declare(int argc, char **argv)
{
	enum locklint_command_result result;

	if (argc == 0)
		return (command_parse_error("declare requires a declaration kind"));
	if (strncmp(argv[0], "--", 2) == 0)
		return (declare_options(argc, argv));
	if (strcmp(argv[0], "entry") == 0)
		return (declare_entry(argc, argv));
	if (strcmp(argv[0], "targets") == 0)
		return (declare_targets(argc, argv));
	if (strcmp(argv[0], "contract") == 0)
		return (declare_contract(argc, argv));
	if (strcmp(argv[0], "lock-order") == 0)
		return (declare_lock_order(argc, argv));
	if (strcmp(argv[0], "mutex-protects-data") == 0)
		return (declare_mutex_protection(argc, argv));
	if (strcmp(argv[0], "readable") != 0)
		return (not_implemented("declare"));
	if (argc != 2) {
		return (command_parse_error(
		    "declare readable requires one data name"));
	}
	result = locklint_declare_readable(argv[1], command_parse_path(),
	    command_parse_line());
	switch (result) {
	case LOCKLINT_COMMAND_OK:
		return (0);
	case LOCKLINT_COMMAND_INVALID_NAME:
		return (command_parse_error("invalid data name '%s'", argv[1]));
	case LOCKLINT_COMMAND_UNRESOLVED_NAME:
		return (command_parse_error("unresolved data name '%s'",
		    argv[1]));
	case LOCKLINT_COMMAND_INCONSISTENT_TYPE:
		return (command_parse_error(
		    "inconsistently defined type in data name '%s'", argv[1]));
	default:
		return (command_parse_error(
		    "internal error resolving data name '%s'", argv[1]));
	}
}

int
cmd_ignore(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return (not_implemented("ignore"));
}

int
cmd_lock_role_protects_data(int argc, char **argv)
{
	enum locklint_command_result result;
	const char *problem;

	if (argc < 2) {
		return (command_parse_error("lock-role-protects-data requires "
		    "one lock role and at least one data name"));
	}
	result = locklint_declare_lock_role(argv[0], (size_t)(argc - 1),
	    (const char *const *)&argv[1], &problem, command_parse_path(),
	    command_parse_line());
	switch (result) {
	case LOCKLINT_COMMAND_OK:
		return (0);
	case LOCKLINT_COMMAND_INVALID_NAME:
		return (command_parse_error("invalid %s name '%s'",
		    problem == argv[0] ? "lock role" : "data", problem));
	case LOCKLINT_COMMAND_UNRESOLVED_NAME:
		return (command_parse_error("unresolved %s name '%s'",
		    problem == argv[0] ? "lock role" : "data", problem));
	case LOCKLINT_COMMAND_AMBIGUOUS_NAME:
		return (command_parse_error("ambiguous %s name '%s'",
		    problem == argv[0] ? "lock role" : "data", problem));
	case LOCKLINT_COMMAND_INCONSISTENT_TYPE:
		return (command_parse_error(
		    "inconsistently defined type in %s name '%s'",
		    problem == argv[0] ? "lock role" : "data", problem));
	default:
		return (command_parse_error(
		    "internal error resolving lock-role protection"));
	}
}

static bool
command_identifier_valid(const char *name)
{
	const unsigned char *cursor = (const unsigned char *)name;

	if (*cursor != '_' && !isalpha(*cursor))
		return (false);
	for (cursor++; *cursor != '\0'; cursor++) {
		if (*cursor != '_' && !isalnum(*cursor))
			return (false);
	}
	return (true);
}

int
cmd_merge_instances(int argc, char **argv)
{
	int i;

	if (argc == 0)
		return (command_parse_error(
		    "merge-instances requires at least one type name"));
	for (i = 0; i < argc; i++) {
		enum type_merge_instances_result result;

		if (!command_identifier_valid(argv[i])) {
			return (command_parse_error("invalid type name '%s'",
			    argv[i]));
		}
		result = type_merge_instances(built_in_ident(argv[i]));
		switch (result) {
		case TYPE_MERGE_INSTANCES_OK:
			break;
		case TYPE_MERGE_INSTANCES_UNRESOLVED:
			return (command_parse_error("unresolved type name '%s'",
			    argv[i]));
		case TYPE_MERGE_INSTANCES_NOT_AGGREGATE:
			return (command_parse_error(
			    "type name '%s' does not name a structure or union",
			    argv[i]));
		case TYPE_MERGE_INSTANCES_INCONSISTENT:
			return (command_parse_error(
			    "inconsistently defined type name '%s'", argv[i]));
		default:
			return (command_parse_error(
			    "internal error resolving type name '%s'",
			    argv[i]));
		}
	}
	return (0);
}

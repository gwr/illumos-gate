#!/bin/ksh
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Characterize whether a nested explanatory scheme replaces a containing
# mutex policy across separately loaded OSLL databases, in either load order.
#

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"

run_command()
{
	name=$1
	shift

	"$@" >"$OSLL_RESULT_DIR/$name.out" 2>&1
	status=$?
	print -- "$status" >"$OSLL_RESULT_DIR/$name.status"
	return "$status"
}

run_session()
{
	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_command load-first "$LOCK_LINT" load "$OSLL_FIRST" || exit $?
	if [[ -n "$OSLL_SECOND" ]]; then
		run_command load-second "$LOCK_LINT" load "$OSLL_SECOND" ||
		    exit $?
	fi
	run_command declare-root "$LOCK_LINT" declare root \
	    "$OSLL_ROOT" || exit $?
	run_command vars "$LOCK_LINT" vars
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?
	case "$analyze_status" in
	0|5)
		run_command vars-after "$LOCK_LINT" vars -a
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac
}

if [[ "${OSLL_SESSION_MODE:-0}" == 1 ]]; then
	run_session
fi

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
ACCESS_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/scheme-mutex-precedence-access.c"
SCHEME_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/scheme-mutex-precedence-scheme.c"
LOCAL_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/scheme-mutex-precedence-local.c"
EXACT_MUTEX_FIRST_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/scheme-mutex-precedence-exact-mutex-first.c"
EXACT_SCHEME_FIRST_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/scheme-mutex-precedence-exact-scheme-first.c"
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"

if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi
for source in "$ACCESS_SOURCE" "$SCHEME_SOURCE" "$LOCAL_SOURCE" \
    "$EXACT_MUTEX_FIRST_SOURCE" "$EXACT_SCHEME_FIRST_SOURCE"
do
	if [[ ! -f "$source" ]]; then
		print -u2 "test source not found: $source"
		exit 1
	fi
done

timestamp=$(date '+%Y%m%d-%H%M%S')
BASE_RESULT_DIR="$RESULTS_ROOT/scheme-mutex-precedence-$timestamp-$$"
build_dir="$BASE_RESULT_DIR/ll"

mkdir -p "$build_dir" || exit 1

print "Compiling precedence inputs"
(
	cd "$build_dir" || exit 1
	"$CC" -Zll "$ACCESS_SOURCE" &&
	    "$CC" -Zll "$SCHEME_SOURCE" &&
	    "$CC" -Zll "$LOCAL_SOURCE" &&
	    "$CC" -Zll "$EXACT_MUTEX_FIRST_SOURCE" &&
	    "$CC" -Zll "$EXACT_SCHEME_FIRST_SOURCE"
) >"$BASE_RESULT_DIR/compile.out" 2>&1
compile_status=$?
print -- "$compile_status" >"$BASE_RESULT_DIR/compile.status"
if (( compile_status != 0 )); then
	print -u2 "cc -Zll failed; see $BASE_RESULT_DIR/compile.out"
	exit "$compile_status"
fi

access_ll="$build_dir/scheme-mutex-precedence-access.ll"
scheme_ll="$build_dir/scheme-mutex-precedence-scheme.ll"
local_ll="$build_dir/scheme-mutex-precedence-local.ll"
exact_mutex_first_ll="$build_dir/scheme-mutex-precedence-exact-mutex-first.ll"
exact_scheme_first_ll="$build_dir/scheme-mutex-precedence-exact-scheme-first.ll"
for database in "$access_ll" "$scheme_ll" "$local_ll" \
    "$exact_mutex_first_ll" "$exact_scheme_first_ll"
do
	if [[ ! -f "$database" ]]; then
		ls -la "$build_dir" >>"$BASE_RESULT_DIR/compile.out" 2>&1
		print -u2 "cc -Zll did not create $database"
		exit 1
	fi
done

run_order()
{
	name=$1
	OSLL_FIRST=$2
	OSLL_SECOND=$3
	OSLL_ROOT=$4
	OSLL_RESULT_DIR="$BASE_RESULT_DIR/$name"
	context_tmp="$OSLL_RESULT_DIR/tmp"

	mkdir -p "$context_tmp" || return 1
	OSLL_SESSION_MODE=1
	TMPDIR="$context_tmp"
	export OSLL_FIRST OSLL_SECOND OSLL_ROOT OSLL_RESULT_DIR \
	    OSLL_SESSION_MODE TMPDIR

	print "Running OSLL order $name"
	"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
	session_status=$?
	print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
	if (( session_status != 0 )); then
		print -u2 "OSLL order $name failed with status $session_status"
		print -u2 "see $OSLL_RESULT_DIR/session.out"
		return "$session_status"
	fi
	return 0
}

run_order mutex-then-scheme "$access_ll" "$scheme_ll" \
    check_scheme_mutex_precedence || exit $?
run_order scheme-then-mutex "$scheme_ll" "$access_ll" \
    check_scheme_mutex_precedence || exit $?
run_order same-translation-unit "$local_ll" "" \
    check_scheme_mutex_precedence || exit $?
run_order exact-mutex-first "$exact_mutex_first_ll" "" \
    check_exact_mutex_first || exit $?
run_order exact-scheme-first "$exact_scheme_first_ll" "" \
    check_exact_scheme_first || exit $?

print "Results: $BASE_RESULT_DIR"

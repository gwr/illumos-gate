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
# Characterize warnings for locks retained across a condition-variable wait.
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

	run_command load "$LOCK_LINT" load "$OSLL_LL_FILE" || exit $?
	run_command declare-root "$LOCK_LINT" declare root \
	    "$OSLL_ROOT" || exit $?
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?
	case "$analyze_status" in
	0|5)
		run_command funcs "$LOCK_LINT" funcs -a
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status " \
		    "$analyze_status"
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/condition-wait-other-lock.c"
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"

if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi
if [[ ! -f "$SOURCE" ]]; then
	print -u2 "test source not found: $SOURCE"
	exit 1
fi

timestamp=$(date '+%Y%m%d-%H%M%S')
run_root="$RESULTS_ROOT/condition-wait-other-lock-$timestamp-$$"

mkdir -p "$run_root" || exit 1

run_case()
{
	name=$1
	OSLL_ROOT=$2
	variant=$3
	OSLL_RESULT_DIR="$run_root/$name"
	build_dir="$OSLL_RESULT_DIR/ll"
	context_tmp="$OSLL_RESULT_DIR/tmp"

	mkdir -p "$build_dir" "$context_tmp" || return 1
	print "Compiling condition-wait case $name"
	(
		cd "$build_dir" || exit 1
		"$CC" -Zll -DWAIT_OTHER_VARIANT="$variant" "$SOURCE"
	) >"$OSLL_RESULT_DIR/compile.out" 2>&1
	compile_status=$?
	print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"
	if (( compile_status != 0 )); then
		print -u2 "cc -Zll failed for case $name"
		print -u2 "see $OSLL_RESULT_DIR/compile.out"
		return "$compile_status"
	fi

	OSLL_LL_FILE="$build_dir/condition-wait-other-lock.ll"
	if [[ ! -f "$OSLL_LL_FILE" ]]; then
		ls -la "$build_dir" >>"$OSLL_RESULT_DIR/compile.out" 2>&1
		print -u2 "cc -Zll did not create $OSLL_LL_FILE"
		return 1
	fi

	OSLL_SESSION_MODE=1
	TMPDIR="$context_tmp"
	export OSLL_LL_FILE OSLL_RESULT_DIR OSLL_ROOT OSLL_SESSION_MODE TMPDIR

	print "Running condition-wait case $name"
	"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
	session_status=$?
	print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
	if (( session_status != 0 )); then
		print -u2 "OSLL case $name failed with status $session_status"
		print -u2 "see $OSLL_RESULT_DIR/session.out"
		return "$session_status"
	fi
}

run_case wait-only wait_without_other_lock 1 || exit $?
run_case one-other wait_with_one_other_lock 2 || exit $?
run_case two-other wait_with_two_other_locks 3 || exit $?
run_case maybe-other wait_with_maybe_other_lock 4 || exit $?
run_case wrapped wait_with_other_lock_wrapped 5 || exit $?

print "Results: $run_root"

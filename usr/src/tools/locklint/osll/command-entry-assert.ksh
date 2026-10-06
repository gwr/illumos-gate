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
# Characterize Old Solaris Lock Lint command assertions that require a lock
# to be held when entering one or more functions.
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
	run_command funcs-before "$LOCK_LINT" funcs -a
	run_command locks-before "$LOCK_LINT" locks
	set -A functions -- $OSLL_FUNCTIONS
	case "$OSLL_ASSERT_KIND" in
	mutex|rw-write)
		run_command assert "$LOCK_LINT" assert "$OSLL_LOCK" \
		    protects "${functions[@]}" || exit $?
		;;
	rw-read)
		run_command assert "$LOCK_LINT" assert "$OSLL_LOCK" \
		    protects reads in "${functions[@]}" || exit $?
		;;
	*)
		print -u2 "unknown assertion kind: $OSLL_ASSERT_KIND"
		exit 1
		;;
	esac
	for root in $OSLL_ROOTS
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "$root" || exit $?
	done
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?
	case "$analyze_status" in
	0|5)
		run_command funcs-after "$LOCK_LINT" funcs -a
		run_command locks-after "$LOCK_LINT" locks
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/command-entry-assert.c"
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
run_root="$RESULTS_ROOT/command-entry-assert-$timestamp-$$"

run_case()
{
	name=$1
	variant=$2
	assert_kind=$3
	lock=$4
	functions=$5
	shift 5
	OSLL_RESULT_DIR="$run_root/$name"
	build_dir="$OSLL_RESULT_DIR/ll"
	TMPDIR="$OSLL_RESULT_DIR/tmp"

	mkdir -p "$build_dir" "$TMPDIR" || return 1
	print "Compiling command-entry-assert case $name"
	(
		cd "$build_dir" || exit 1
		"$CC" -Zll -D_KERNEL -D__lock_lint=1 -DDEBUG=1 \
		    -DENTRY_ASSERT_VARIANT="$variant" "$SOURCE"
	) >"$OSLL_RESULT_DIR/compile.out" 2>&1
	compile_status=$?
	print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"
	if (( compile_status != 0 )); then
		print -u2 "cc -Zll failed for case $name"
		print -u2 "see $OSLL_RESULT_DIR/compile.out"
		return "$compile_status"
	fi

	OSLL_LL_FILE="$build_dir/command-entry-assert.ll"
	if [[ ! -f "$OSLL_LL_FILE" ]]; then
		print -u2 "cc -Zll did not create $OSLL_LL_FILE"
		return 1
	fi

	OSLL_SESSION_MODE=1
	OSLL_ASSERT_KIND="$assert_kind"
	OSLL_LOCK="$lock"
	OSLL_FUNCTIONS="$functions"
	OSLL_ROOTS="$*"
	export OSLL_LL_FILE OSLL_RESULT_DIR OSLL_SESSION_MODE
	export OSLL_ASSERT_KIND OSLL_LOCK OSLL_FUNCTIONS OSLL_ROOTS TMPDIR

	print "Running OSLL command-entry-assert case $name"
	"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
	session_status=$?
	print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
	if (( session_status != 0 )) ||
	    [[ ! -f "$OSLL_RESULT_DIR/analyze.status" ]]; then
		print -u2 "OSLL case $name failed with status $session_status"
		print -u2 "see $OSLL_RESULT_DIR/session.out"
		return 1
	fi
	analyze_status=$(<"$OSLL_RESULT_DIR/analyze.status")
	print "OSLL case $name analyze status: $analyze_status"
}

run_case global 1 mutex :entry_assert_global_mutex \
    command_global_helper \
    command_global_correct command_global_incorrect || exit $?
run_case mutex 2 mutex entry_assert_state::mutex \
    command_mutex_helper \
    command_mutex_correct command_mutex_incorrect || exit $?
run_case rw-read 3 rw-read entry_assert_state::rwlock \
    command_read_helper \
    command_read_correct command_read_writer \
    command_read_incorrect || exit $?
run_case rw-write 4 rw-write entry_assert_state::rwlock \
    command_write_helper \
    command_write_correct command_write_reader \
    command_write_unheld || exit $?
run_case multiple 5 mutex entry_assert_state::mutex \
    "command_multiple_first command_multiple_second" \
    command_multiple_correct command_multiple_incorrect || exit $?
run_case two-formal 6 mutex entry_assert_state::mutex \
    command_two_formal_helper \
    command_two_formal_first command_two_formal_second \
    command_two_formal_unheld || exit $?

print "Results: $run_root"

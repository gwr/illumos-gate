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
# Characterize Old Solaris Lock Lint's inferred consistent-protection
# information and its analysis diagnostics for unannotated data.
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
	run_command help-vars "$LOCK_LINT" help vars
	run_command vars-before "$LOCK_LINT" vars -a
	run_command vars-held-before "$LOCK_LINT" vars -h
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?
	case "$analyze_status" in
	0|5)
		run_command vars-after "$LOCK_LINT" vars -a
		run_command vars-held-after "$LOCK_LINT" vars -h
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/consistent-protection.c"
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
run_root="$RESULTS_ROOT/consistent-protection-$timestamp-$$"

run_case()
{
	name=$1
	OSLL_ROOT=$2
	variant=$3
	OSLL_RESULT_DIR="$run_root/$name"
	build_dir="$OSLL_RESULT_DIR/ll"
	context_tmp="$OSLL_RESULT_DIR/tmp"

	mkdir -p "$build_dir" "$context_tmp" || return 1
	print "Compiling consistent-protection case $name"
	(
		cd "$build_dir" || exit 1
		"$CC" -Zll -DCONSISTENT_PROTECTION_VARIANT="$variant" \
		    "$SOURCE"
	) >"$OSLL_RESULT_DIR/compile.out" 2>&1
	compile_status=$?
	print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"
	if (( compile_status != 0 )); then
		print -u2 "cc -Zll failed for case $name"
		print -u2 "see $OSLL_RESULT_DIR/compile.out"
		return "$compile_status"
	fi

	OSLL_LL_FILE="$build_dir/consistent-protection.ll"
	if [[ ! -f "$OSLL_LL_FILE" ]]; then
		ls -la "$build_dir" >>"$OSLL_RESULT_DIR/compile.out" 2>&1
		print -u2 "cc -Zll did not create $OSLL_LL_FILE"
		return 1
	fi

	OSLL_SESSION_MODE=1
	TMPDIR="$context_tmp"
	export OSLL_LL_FILE OSLL_RESULT_DIR OSLL_ROOT OSLL_SESSION_MODE TMPDIR

	print "Running consistent-protection case $name"
	"$LOCK_LINT" start /bin/ksh "$SCRIPT" \
	    >"$OSLL_RESULT_DIR/session.out" 2>&1
	session_status=$?
	print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
	if (( session_status != 0 )) ||
	    [[ ! -f "$OSLL_RESULT_DIR/analyze.status" ]]; then
		print -u2 "OSLL case $name failed with status $session_status"
		print -u2 "see $OSLL_RESULT_DIR/session.out"
		return 1
	fi
}

run_case all-unlocked protection_all_unlocked 1 || exit $?
run_case one-mutex protection_one_mutex 2 || exit $?
run_case mixed-mutex protection_mixed_mutex 3 || exit $?
run_case two-mutexes protection_two_mutexes 4 || exit $?
run_case common-plus-extra protection_common_plus_extra 5 || exit $?
run_case reads-unlocked protection_reads_unlocked 6 || exit $?
run_case rwlock-modes protection_rwlock_modes 7 || exit $?
run_case no-competition protection_no_competition 8 || exit $?

print "Results: $run_root"

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
# Run the focused cv_wait ownership and reacquisition-order cases under
# Solaris LockLint.
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
	for root in \
	    wait_while_held \
	    wait_without_lock \
	    wait_variants_while_held \
	    wait_sig_without_lock \
	    timedwait_without_lock \
	    timedwait_sig_without_lock \
	    reltimedwait_without_lock \
	    reltimedwait_sig_without_lock \
	    uncommon_waits_while_held \
	    wait_stop_without_lock \
	    timedwait_hires_without_lock \
	    timedwait_sig_hrtime_without_lock \
	    wait_sig_swap_without_lock \
	    wait_sig_swap_core_without_lock \
	    waituntil_sig_without_lock \
	    wrapped_wait_while_held \
	    wrapped_wait_reacquire_inversion \
	    wait_reacquire_in_order \
	    wait_reacquire_inversion \
	    timedwait_sig_reacquire_inversion \
	    wait_sig_swap_reacquire_inversion
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "condition-wait.c:$root" || exit $?
	done

	run_command funcs-before "$LOCK_LINT" funcs -a
	run_command locks-before "$LOCK_LINT" locks
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?

	case "$analyze_status" in
	0|5)
		run_command funcs-after "$LOCK_LINT" funcs -a
		run_command locks-after "$LOCK_LINT" locks
		run_command order "$LOCK_LINT" order
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac
}

if [[ "${OSLL_SESSION_MODE:-0}" = 1 ]]; then
	run_session
fi

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/condition-wait.c"
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
OSLL_RESULT_DIR="$RESULTS_ROOT/condition-wait-$timestamp-$$"
build_dir="$OSLL_RESULT_DIR/ll"
context_tmp="$OSLL_RESULT_DIR/tmp"

mkdir -p "$build_dir" "$context_tmp" || exit 1
print "Compiling condition-wait fixture"
(
	cd "$build_dir" || exit 1
	"$CC" -Zll -D_KERNEL "$SOURCE"
) >"$OSLL_RESULT_DIR/compile.out" 2>&1
compile_status=$?
print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"
if [[ "$compile_status" -ne 0 ]]; then
	print -u2 "cc -Zll failed"
	print -u2 "see $OSLL_RESULT_DIR/compile.out"
	exit "$compile_status"
fi

OSLL_LL_FILE="$build_dir/condition-wait.ll"
if [[ ! -f "$OSLL_LL_FILE" ]]; then
	ls -la "$build_dir" >>"$OSLL_RESULT_DIR/compile.out" 2>&1
	print -u2 "cc -Zll did not create $OSLL_LL_FILE"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$context_tmp"
export OSLL_RESULT_DIR OSLL_LL_FILE OSLL_SESSION_MODE TMPDIR

print "Running OSLL condition-wait fixture"
"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
session_status=$?
print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
if [[ "$session_status" -ne 0 ]]; then
	print -u2 "OSLL condition-wait run failed with status $session_status"
	print -u2 "see $OSLL_RESULT_DIR/session.out"
	exit "$session_status"
fi

analyze_status=$(<"$OSLL_RESULT_DIR/analyze.status")
case "$analyze_status" in
0)
	print "OSLL condition-wait analysis completed without findings."
	;;
5)
	print "OSLL condition-wait analysis completed with findings."
	;;
*)
	print "OSLL condition-wait analysis returned status $analyze_status."
	;;
esac

print "Results: $OSLL_RESULT_DIR"

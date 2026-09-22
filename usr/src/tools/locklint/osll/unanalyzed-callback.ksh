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
# Characterize which unresolved function uses produce OSLL's warning that
# calls to a function have not been analyzed.
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
	run_command funcs-before "$LOCK_LINT" funcs -o
	run_command declare-root "$LOCK_LINT" declare root \
	    unanalyzed_callback_root || exit $?
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?
	case "$analyze_status" in
	0|5)
		run_command funcs-after "$LOCK_LINT" funcs -o
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/unanalyzed-callback.c"
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
run_root="$RESULTS_ROOT/unanalyzed-callback-$timestamp-$$"
build_dir="$run_root/ll"
context_tmp="$run_root/tmp"

mkdir -p "$build_dir" "$context_tmp" || exit 1
print "Compiling unanalyzed callback characterization"
(
	cd "$build_dir" || exit 1
	"$CC" -Zll "$SOURCE"
) >"$run_root/compile.out" 2>&1
compile_status=$?
print -- "$compile_status" >"$run_root/compile.status"
if (( compile_status != 0 )); then
	print -u2 "cc -Zll failed"
	print -u2 "see $run_root/compile.out"
	exit "$compile_status"
fi

OSLL_LL_FILE="$build_dir/unanalyzed-callback.ll"
if [[ ! -f "$OSLL_LL_FILE" ]]; then
	ls -la "$build_dir" >>"$run_root/compile.out" 2>&1
	print -u2 "cc -Zll did not create $OSLL_LL_FILE"
	exit 1
fi

OSLL_RESULT_DIR="$run_root"
OSLL_SESSION_MODE=1
TMPDIR="$context_tmp"
export OSLL_LL_FILE OSLL_RESULT_DIR OSLL_SESSION_MODE TMPDIR

print "Running OSLL unanalyzed callback characterization"
"$LOCK_LINT" start "$SCRIPT" >"$run_root/session.out" 2>&1
session_status=$?
print -- "$session_status" >"$run_root/session.status"
if (( session_status != 0 )); then
	print -u2 "OSLL session failed with status $session_status"
	print -u2 "see $run_root/session.out"
	exit "$session_status"
fi

print "Results: $run_root"

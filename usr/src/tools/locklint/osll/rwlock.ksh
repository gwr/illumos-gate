#!/bin/ksh

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

	run_command load-rwlock "$LOCK_LINT" load "$OSLL_RWLOCK_LL" ||
	    exit $?
	run_command load-rwlock-calls "$LOCK_LINT" load \
	    "$OSLL_RWLOCK_CALLS_LL" || exit $?

	for root in \
	    check_modes \
	    check_mode_merge
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "rwlock.c:$root" || exit $?
	done

	for root in \
	    call_read_as_reader \
	    call_write_as_reader \
	    call_read_as_writer \
	    call_write_as_writer \
	    check_reader_effect \
	    check_writer_effect
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "rwlock-calls.c:$root" || exit $?
	done

	run_command files "$LOCK_LINT" files
	run_command funcs "$LOCK_LINT" funcs
	run_command funcs-roots-before "$LOCK_LINT" funcs -o
	run_command pointer-calls "$LOCK_LINT" pointer calls
	run_command vars "$LOCK_LINT" vars
	run_command funcs-all-before "$LOCK_LINT" funcs -a
	run_command vars-all-before "$LOCK_LINT" vars -a
	run_command locks-before "$LOCK_LINT" locks

	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?

	case "$analyze_status" in
	0|5)
		run_command funcs-all-after "$LOCK_LINT" funcs -a
		run_command funcs-roots-after "$LOCK_LINT" funcs -o
		run_command vars-all-after "$LOCK_LINT" vars -a
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
RWLOCK_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/rwlock.c"
CALLS_SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/rwlock-calls.c"
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"

if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi
if [[ ! -f "$RWLOCK_SOURCE" ]]; then
	print -u2 "test source not found: $RWLOCK_SOURCE"
	exit 1
fi
if [[ ! -f "$CALLS_SOURCE" ]]; then
	print -u2 "test source not found: $CALLS_SOURCE"
	exit 1
fi

timestamp=$(date '+%Y%m%d-%H%M%S')
run_root="$RESULTS_ROOT/rwlock-$timestamp-$$"

run_variant()
{
	variant=$1
	shift

	OSLL_RESULT_DIR="$run_root/$variant"
	build_dir="$OSLL_RESULT_DIR/ll"
	context_tmp="$OSLL_RESULT_DIR/tmp"
	mkdir -p "$build_dir" "$context_tmp" || return 1

	print "Compiling $variant rwlock fixtures"
	(
		cd "$build_dir" || exit 1
		"$CC" -Zll "$@" "$RWLOCK_SOURCE" || exit $?
		"$CC" -Zll "$@" "$CALLS_SOURCE"
	) >"$OSLL_RESULT_DIR/compile.out" 2>&1
	compile_status=$?
	print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"

	if [[ "$compile_status" -ne 0 ]]; then
		print -u2 "cc -Zll failed for $variant; see " \
		    "$OSLL_RESULT_DIR/compile.out"
		return "$compile_status"
	fi

	OSLL_RWLOCK_LL="$build_dir/rwlock.ll"
	OSLL_RWLOCK_CALLS_LL="$build_dir/rwlock-calls.ll"
	if [[ ! -f "$OSLL_RWLOCK_LL" ||
	    ! -f "$OSLL_RWLOCK_CALLS_LL" ]]; then
		ls -la "$build_dir" >>"$OSLL_RESULT_DIR/compile.out" 2>&1
		print -u2 "cc -Zll did not create both $variant databases"
		print -u2 "see $OSLL_RESULT_DIR/compile.out"
		return 1
	fi

	OSLL_SESSION_MODE=1
	TMPDIR="$context_tmp"
	export OSLL_RESULT_DIR OSLL_RWLOCK_LL OSLL_RWLOCK_CALLS_LL
	export OSLL_SESSION_MODE TMPDIR

	print "Running OSLL for $variant rwlocks"
	"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
	session_status=$?
	print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"

	if [[ "$session_status" -ne 0 ]]; then
		print -u2 "OSLL $variant session failed with status " \
		    "$session_status"
		print -u2 "see $OSLL_RESULT_DIR/session.out"
		return "$session_status"
	fi

	analyze_status=$(<"$OSLL_RESULT_DIR/analyze.status")
	case "$analyze_status" in
	0)
		print "OSLL $variant analysis completed without findings."
		;;
	5)
		print "OSLL $variant analysis completed with findings."
		;;
	*)
		print "OSLL $variant analysis returned status $analyze_status."
		;;
	esac
	return 0
}

run_variant user -DLOCKLINT_RWLOCK_CORE_ONLY || exit $?
run_variant kernel -DLOCKLINT_RWLOCK_CORE_ONLY -D_KERNEL || exit $?

print "Results: $run_root"

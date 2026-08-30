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

	run_command load "$LOCK_LINT" load "$OSLL_LL_FILE" || exit $?

	for root in \
	    check_global_names \
	    check_generated_names \
	    check_recursive_names \
	    check_override \
	    check_embedded_type \
	    check_embedded_global_lock
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "annotation-names.c:$root" || exit $?
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/annotation-names.c"
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
OSLL_RESULT_DIR="$RESULTS_ROOT/annotation-names-$timestamp-$$"
build_dir="$OSLL_RESULT_DIR/ll"
context_tmp="$OSLL_RESULT_DIR/tmp"

mkdir -p "$build_dir" "$context_tmp" || exit 1

print "Compiling $SOURCE"
(
	cd "$build_dir" || exit 1
	"$CC" -Zll "$SOURCE"
) >"$OSLL_RESULT_DIR/compile.out" 2>&1
compile_status=$?
print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"

if [[ "$compile_status" -ne 0 ]]; then
	print -u2 "cc -Zll failed; see $OSLL_RESULT_DIR/compile.out"
	exit "$compile_status"
fi

OSLL_LL_FILE="$build_dir/annotation-names.ll"
if [[ ! -f "$OSLL_LL_FILE" ]]; then
	ls -la "$build_dir" >>"$OSLL_RESULT_DIR/compile.out" 2>&1
	print -u2 "cc -Zll did not create $OSLL_LL_FILE"
	print -u2 "see $OSLL_RESULT_DIR/compile.out"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$context_tmp"
export OSLL_RESULT_DIR OSLL_LL_FILE OSLL_SESSION_MODE TMPDIR

print "Running OSLL"
"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
session_status=$?
print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"

if [[ "$session_status" -ne 0 ]]; then
	print -u2 "OSLL session failed with status $session_status"
	print -u2 "see $OSLL_RESULT_DIR/session.out"
	exit "$session_status"
fi

analyze_status=$(<"$OSLL_RESULT_DIR/analyze.status")
case "$analyze_status" in
0)
	print "OSLL analysis completed without reported violations."
	;;
5)
	print "OSLL analysis completed and reported findings."
	;;
*)
	print "OSLL analysis returned status $analyze_status."
	;;
esac

print "Results: $OSLL_RESULT_DIR"

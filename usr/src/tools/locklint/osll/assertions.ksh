#!/bin/ksh
#
# Run the focused protected-data ASSERT case under Solaris LockLint.  The
# fixture root isolates whether OSLL checks data reads preserved from an
# otherwise empty assertion macro.
#

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
DEBUG_VALUE=${DEBUG_VALUE:-1}

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
	run_command declare-root "$LOCK_LINT" declare root \
	    :assertion_expression_access || exit $?
	run_command analyze "$LOCK_LINT" analyze
	analyze_status=$?

	case "$analyze_status" in
	0|5)
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
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/assertions.c"
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"

if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi

timestamp=$(date '+%Y%m%d-%H%M%S')
OSLL_RESULT_DIR="$RESULTS_ROOT/assertions-debug$DEBUG_VALUE-$timestamp-$$"
build_dir="$OSLL_RESULT_DIR/ll"
context_tmp="$OSLL_RESULT_DIR/tmp"

mkdir -p "$build_dir" "$context_tmp" || exit 1
print "Compiling assertions fixture"
(
	cd "$build_dir" || exit 1
	"$CC" -Zll -D__lock_lint=1 -DDEBUG="$DEBUG_VALUE" "$SOURCE"
) >"$OSLL_RESULT_DIR/compile.out" 2>&1
compile_status=$?
print -- "$compile_status" >"$OSLL_RESULT_DIR/compile.status"
if [[ "$compile_status" -ne 0 ]]; then
	print -u2 "cc -Zll failed; see $OSLL_RESULT_DIR/compile.out"
	exit "$compile_status"
fi

OSLL_LL_FILE="$build_dir/assertions.ll"
if [[ ! -f "$OSLL_LL_FILE" ]]; then
	print -u2 "cc -Zll did not create $OSLL_LL_FILE"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$context_tmp"
export OSLL_RESULT_DIR OSLL_LL_FILE OSLL_SESSION_MODE TMPDIR

print "Running OSLL assertions fixture"
"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
session_status=$?
print -- "$session_status" >"$OSLL_RESULT_DIR/session.status"
if [[ "$session_status" -ne 0 ]]; then
	print -u2 "OSLL assertions run failed with status $session_status"
	print -u2 "see $OSLL_RESULT_DIR/session.out"
	exit "$session_status"
fi

print "Results: $OSLL_RESULT_DIR"

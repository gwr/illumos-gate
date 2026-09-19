#!/bin/ksh
#
# Characterize ASSERT(NO_COMPETING_THREADS) with Old Solaris Lock Lint.
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
	run_command load "$LOCK_LINT" load "$OSLL_LL_FILE" || exit $?
	for root in assertion_competition_immediate \
	    assertion_competition_later assertion_competition_one_branch \
	    assertion_competition_callee assertion_competition_reentered \
	    assertion_competition_invalid
	do
		run_command "declare-root-$root" "$LOCK_LINT" declare root \
		    "assertion-competition.c:$root" || exit $?
	done
	run_command analyze "$LOCK_LINT" analyze
	status=$?
	[[ "$status" = 0 || "$status" = 5 ]] || exit "$status"
	exit 0
}

if [[ "${OSLL_SESSION_MODE:-0}" = 1 ]]; then
	run_session
fi

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
SOURCE="$REPO_ROOT/usr/src/tools/locklint/tests/assertion-competition.c"
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"
timestamp=$(date '+%Y%m%d-%H%M%S')
OSLL_RESULT_DIR="$RESULTS_ROOT/assertion-competition-$timestamp-$$"
build_dir="$OSLL_RESULT_DIR/ll"
TMPDIR="$OSLL_RESULT_DIR/tmp"

mkdir -p "$build_dir" "$TMPDIR" || exit 1
(
	cd "$build_dir" || exit 1
	"$CC" -Zll -D__lock_lint=1 -DDEBUG=1 "$SOURCE"
) >"$OSLL_RESULT_DIR/compile.out" 2>&1 || exit $?

OSLL_LL_FILE="$build_dir/assertion-competition.ll"
OSLL_SESSION_MODE=1
export OSLL_RESULT_DIR OSLL_LL_FILE OSLL_SESSION_MODE TMPDIR

"$LOCK_LINT" start "$SCRIPT" >"$OSLL_RESULT_DIR/session.out" 2>&1
status=$?
print -- "$status" >"$OSLL_RESULT_DIR/session.status"
print "Results: $OSLL_RESULT_DIR"
exit "$status"

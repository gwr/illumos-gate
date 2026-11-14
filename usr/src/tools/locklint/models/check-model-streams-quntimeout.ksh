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
# Check ordinary caller-lock obligations across quntimeout's possible wait.
# Module-wide close entry exempts the framework claim, retaining driver checks.
# Exact warning totals also reject lost ownership after cancellation returns.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-quntimeout.c"

run_case()
{
	name=$1
	held=$2
	flags=${3:-D_MP}
	commands=${4:-$SCRIPT_DIR/streams-quntimeout.cf}
	caller=${5:-quntimeout}
	extra_cpp=${6:-}
	expected=${7:-$held}
	output="$SCRIPT_DIR/streams-quntimeout-$name.raw"

	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none --dump-callgraph \
		    --cf "$commands" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    "-DMODEL_FLAGS=$flags" "-DQUNTIMEOUT_HOLD_DRIVER_LOCK=$held" \
		    $extra_cpp \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "quntimeout $name analysis failed with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	waits=$(grep -c '\[lock-held-during-wait\]$' "$output")
	warnings=$(grep -c 'warning:' "$output")
	if (( waits != expected || warnings != expected )); then
		print -u2 "quntimeout $name: waits=$waits, warnings=$warnings " \
		    "(expected $expected each); see $output"
		return 1
	fi
	if ! grep -q \
	    "^  call $INPUT:.* resolved quntimeout tu=$MODEL\$" "$output"; then
		print -u2 "quntimeout $name did not resolve the representative"
		print -u2 "see $output"
		return 1
	fi
	if (( expected != 0 )) && ! grep -q \
	    "lock is held on a path through call to '$caller'\$" "$output"; then
		print -u2 "quntimeout $name lacks caller provenance; see $output"
		return 1
	fi
	rm -f "$output"
}

run_case driver 1 || exit $?
run_case unlocked 0 || exit $?
run_case inner 0 '(D_MP|D_MTPERMOD)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" || exit $?
run_case inner-driver 1 '(D_MP|D_MTPERMOD)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close || exit $?

# Shared close entry with exclusive and shared callback configurations.
shared_flags='(D_MP|D_MTPERMOD|D_MTPUTSHARED|_D_MTOCSHARED)'
callback_flags='(D_MP|D_MTPERMOD|D_MTPUTSHARED|_D_MTOCSHARED|_D_MTCBSHARED)'
run_case inner-shared 0 "$shared_flags" \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" || exit $?
run_case inner-shared-driver 1 "$shared_flags" \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close || exit $?
run_case inner-shared-callback 0 "$callback_flags" \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" || exit $?
run_case inner-shared-callback-driver 1 "$callback_flags" \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close || exit $?


# Per-queue writer entry must not exempt a different queue's perimeter.
run_case perq 0 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" || exit $?
run_case perq-driver 1 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close || exit $?
run_case perq-other 0 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close \
    '-DCANCEL_HOLD_OTHER_QUEUE' 1 || exit $?
run_case perq-other-driver 1 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout-inner.cf" streams_quntimeout_close \
    '-DCANCEL_HOLD_OTHER_QUEUE' 2 || exit $?
run_case perq-unlocked 0 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout.cf" quntimeout \
    '-DCANCEL_NO_ENTRY' || exit $?
run_case perq-other-only 0 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout.cf" quntimeout \
    '-DCANCEL_NO_ENTRY -DCANCEL_HOLD_OTHER_QUEUE' 1 || exit $?


# An unbound queue tests the guarded exemption independently of discovery.
run_case perq-opaque 0 '(D_MP|D_MTPERQ)' \
    "$SCRIPT_DIR/streams-quntimeout.cf" quntimeout \
    '-DCANCEL_NO_ENTRY -DCANCEL_OPAQUE_QUEUE' || exit $?

print "New locklint quntimeout model preserves locks and scopes wait exemptions."

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
# Validate qprocsoff() wait exemptions under framework close-entry perimeters
# while retaining diagnostics for an ordinary driver mutex.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qproc-close.c"

run_case()
{
	name=$1
	flags=$2
	driver_lock=$3
	commands=$4
	expected_waits=$5
	output="$SCRIPT_DIR/streams-qproc-close-$name.raw"

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none \
		    --cf "$commands" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    "-DMODEL_FLAGS=$flags" \
		    "-DQPROC_HOLD_DRIVER_LOCK=$driver_lock" \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "STREAMS qproc close $name analysis failed with " \
		    "status $status"
		print -u2 "see $output"
		return "$status"
	fi
	if grep -q "\\[invalid-wait-exemption\\]$" "$output"; then
		print -u2 "STREAMS qproc close $name has an invalid exemption"
		print -u2 "see $output"
		return 1
	fi
	waits=$(grep -c "\\[lock-held-during-wait\\]$" "$output")
	if (( waits != expected_waits )); then
		print -u2 "STREAMS qproc close $name produced $waits " \
		    "held-during-wait findings instead of $expected_waits"
		print -u2 "see $output"
		return 1
	fi
	if (( expected_waits != 0 )) && ! grep -q \
	    "lock is held on a path through call to 'streams_qproc_close'\$" \
	    "$output"; then
		print -u2 "STREAMS qproc close $name lacks caller provenance"
		return 1
	fi

	rm -f "$output"
}

run_case inner '(D_MP|D_MTPERMOD)' 0 \
    "$SCRIPT_DIR/streams-qproc-close-inner.cf" 0 || exit $?
run_case outer '(D_MP|D_MTOUTPERIM)' 0 \
    "$SCRIPT_DIR/streams-qproc-close-outer.cf" 0 || exit $?
run_case combined '(D_MP|D_MTPERMOD|D_MTOUTPERIM)' 0 \
    "$SCRIPT_DIR/streams-qproc-close-combined.cf" 0 || exit $?
run_case combined-driver '(D_MP|D_MTPERMOD|D_MTOUTPERIM)' 1 \
    "$SCRIPT_DIR/streams-qproc-close-combined.cf" 1 || exit $?

print "New locklint qproc close wait exemptions work."

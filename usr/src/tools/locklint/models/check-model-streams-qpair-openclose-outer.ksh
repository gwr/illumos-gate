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
# Validate valid inner and outer modes for queue-pair open and close entry.
# Wrong-pair inner and missing-outer controls remain unsafe.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qpair-openclose-outer.c"
COMMANDS="$SCRIPT_DIR/streams-qpair-openclose-outer.cf"

run_case()
{
	name=$1
	flags=$2
	expected_fixture=$3
	output="$SCRIPT_DIR/streams-qpair-openclose-outer-$name.raw"

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none \
		    --cf "$COMMANDS" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    "-DMODEL_FLAGS=$flags" \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "STREAMS queue-pair outer open/close $name failed " \
		    "with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	warnings=$(grep -c "warning:.*\\[unprotected-access\\]$" "$output")
	fixture=$(grep -c \
	    "check-model-streams-qpair-openclose-outer.c:.*protection was not established" \
	    "$output")
	model=$(grep -c \
	    "streams-dki-model.c:.*protection was not established" "$output")
	all_warnings=$(grep -c "warning:" "$output")
	if (( warnings != 20 || all_warnings != 20 ||
	    fixture != expected_fixture || model != 0 )); then
		print -u2 "Unexpected queue-pair outer open/close $name findings:"
		print -u2 "  warnings=$warnings (expected 20)"
		print -u2 "  all warnings=$all_warnings (expected 20)"
		print -u2 "  fixture provenance=$fixture " \
		    "(expected $expected_fixture)"
		print -u2 "  model provenance=$model (expected 0)"
		print -u2 "see $output"
		return 1
	fi

	rm -f "$output"
}

run_case d-mp D_MP 65 || exit $?
run_case d-mtouter '(D_MP|D_MTOUTPERIM)' 49 || exit $?
run_case d-mtouter-excl '(D_MP|D_MTOUTPERIM|D_MTOCEXCL)' 41 || exit $?
run_case d-mtqpair-outer '(D_MP|D_MTQPAIR|D_MTOUTPERIM)' 33 || exit $?
run_case d-mtqpair-outer-excl \
    '(D_MP|D_MTQPAIR|D_MTOUTPERIM|D_MTOCEXCL)' 25 || exit $?
run_case d-mtqpair-shared-outer \
    '(D_MP|D_MTQPAIR|D_MTPUTSHARED|_D_MTOCSHARED|D_MTOUTPERIM)' 41 ||
    exit $?
run_case d-mtqpair-shared-outer-excl \
    '(D_MP|D_MTQPAIR|D_MTPUTSHARED|_D_MTOCSHARED|D_MTOUTPERIM|D_MTOCEXCL)' \
    33 || exit $?

print "New locklint D_MTQPAIR open/close outer composition works."

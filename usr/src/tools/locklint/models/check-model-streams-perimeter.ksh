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
# Validate single-module STREAMS put entry under inner and outer perimeters.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-perimeter.c"

run_case()
{
	name=$1
	flags=$2
	commands=$3
	expected_modeled=$4
	expected_outer=$5
	expected_direct=$6
	output="$SCRIPT_DIR/streams-perimeter-$name.raw"

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --dump-callgraph --cf "$commands" \
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
		print -u2 "STREAMS perimeter $name analysis failed with " \
		    "status $status"
		print -u2 "see $output"
		return "$status"
	fi
	if ! grep -q \
	    "^  call $INPUT:.* resolved putnext tu=$MODEL\$" "$output"; then
		print -u2 "STREAMS perimeter $name did not resolve putnext"
		print -u2 "see $output"
		return 1
	fi
	if grep -q "asserted-lock-requirement" "$output"; then
		print -u2 "STREAMS perimeter $name has an entry-mode mismatch"
		print -u2 "see $output"
		return 1
	fi

	modeled=$(grep -c "'modeled_data'.*\\[unprotected-access\\]$" "$output")
	outer=$(grep -c "'outer_data'.*\\[unprotected-access\\]$" "$output")
	direct=$(grep -c "'direct_data'.*\\[unprotected-access\\]$" "$output")
	if (( modeled != expected_modeled || outer != expected_outer ||
	    direct != expected_direct )); then
		print -u2 "Unexpected STREAMS perimeter $name findings:"
		print -u2 "  modeled=$modeled (expected $expected_modeled)"
		print -u2 "  outer=$outer (expected $expected_outer)"
		print -u2 "  direct=$direct (expected $expected_direct)"
		print -u2 "see $output"
		return 1
	fi

	rm -f "$output"
}

run_case d-mp D_MP "$SCRIPT_DIR/streams-perimeter.cf" 2 2 2 || exit $?
run_case d-mtpermod '(D_MP|D_MTPERMOD)' \
    "$SCRIPT_DIR/streams-perimeter-mtpermod.cf" 0 2 2 || exit $?
run_case d-mtpermod-putshared \
    '(D_MP|D_MTPERMOD|D_MTPUTSHARED)' \
    "$SCRIPT_DIR/streams-perimeter-mtpermod-putshared.cf" 1 2 2 || exit $?
run_case d-mtouter '(D_MP|D_MTOUTPERIM)' \
    "$SCRIPT_DIR/streams-perimeter-outer.cf" 2 1 2 || exit $?
run_case d-mtpermod-outer '(D_MP|D_MTPERMOD|D_MTOUTPERIM)' \
    "$SCRIPT_DIR/streams-perimeter-mtpermod-outer.cf" 0 1 2 || exit $?

print "New locklint single-module STREAMS perimeter entry works."

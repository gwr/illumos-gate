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
# Validate single-module STREAMS open and close entry under inner and outer
# perimeters.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-openclose.c"

run_case()
{
	name=$1
	flags=$2
	commands=$3
	expected_inner=$4
	expected_outer=$5
	expected_direct=$6
	output="$SCRIPT_DIR/streams-openclose-$name.raw"

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
		print -u2 "STREAMS open/close $name analysis failed with " \
		    "status $status"
		print -u2 "see $output"
		return "$status"
	fi
	if grep -q "asserted-lock-requirement" "$output"; then
		print -u2 "STREAMS open/close $name has an entry-mode mismatch"
		print -u2 "see $output"
		return 1
	fi
	for function in streams_modeled_open streams_modeled_close
	do
		if ! grep -q "resolved-indirect $function " "$output"; then
			print -u2 "STREAMS open/close $name did not resolve " \
			    "$function"
			print -u2 "see $output"
			return 1
		fi
	done

	open_inner=$(grep -c \
	    "'open_inner_data'.*\\[unprotected-access\\]$" "$output")
	close_inner=$(grep -c \
	    "'close_inner_data'.*\\[unprotected-access\\]$" "$output")
	open_outer=$(grep -c \
	    "'open_outer_data'.*\\[unprotected-access\\]$" "$output")
	close_outer=$(grep -c \
	    "'close_outer_data'.*\\[unprotected-access\\]$" "$output")
	direct=$(grep -c \
	    "'openclose_direct_data'.*\\[unprotected-access\\]$" "$output")
	if (( open_inner != expected_inner ||
	    close_inner != expected_inner ||
	    open_outer != expected_outer ||
	    close_outer != expected_outer ||
	    direct != expected_direct )); then
		print -u2 "Unexpected STREAMS open/close $name findings:"
		print -u2 "  open inner=$open_inner " \
		    "(expected $expected_inner)"
		print -u2 "  close inner=$close_inner " \
		    "(expected $expected_inner)"
		print -u2 "  open outer=$open_outer " \
		    "(expected $expected_outer)"
		print -u2 "  close outer=$close_outer " \
		    "(expected $expected_outer)"
		print -u2 "  direct=$direct (expected $expected_direct)"
		print -u2 "see $output"
		return 1
	fi
}

run_case d-mp D_MP "$SCRIPT_DIR/streams-openclose.cf" 2 2 2 || exit $?
run_case d-mtpermod '(D_MP|D_MTPERMOD)' \
    "$SCRIPT_DIR/streams-openclose-mtpermod.cf" 0 2 2 || exit $?
run_case d-mtpermod-shared '(D_MP|D_MTPERMOD|_D_MTOCSHARED)' \
    "$SCRIPT_DIR/streams-openclose-mtpermod-shared.cf" 1 2 2 || exit $?
run_case d-mtouter '(D_MP|D_MTOUTPERIM)' \
    "$SCRIPT_DIR/streams-openclose-outer.cf" 2 1 2 || exit $?
run_case d-mtouter-excl '(D_MP|D_MTOUTPERIM|D_MTOCEXCL)' \
    "$SCRIPT_DIR/streams-openclose-outer-excl.cf" 2 0 2 || exit $?
run_case d-mtpermod-outer '(D_MP|D_MTPERMOD|D_MTOUTPERIM)' \
    "$SCRIPT_DIR/streams-openclose-mtpermod-outer.cf" 0 1 2 || exit $?
run_case d-mtpermod-shared-outer-excl \
    '(D_MP|D_MTPERMOD|_D_MTOCSHARED|D_MTOUTPERIM|D_MTOCEXCL)' \
    "$SCRIPT_DIR/streams-openclose-mtpermod-shared-outer-excl.cf" \
    1 0 2 || exit $?

print "New locklint single-module STREAMS open/close entry works."

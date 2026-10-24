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
# Validate the no-perimeter D_MP control, exclusive D_MTPERQ put entry, and
# its D_MTPUTSHARED reader mode.  The direct callback remains unprotected in
# every case.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-perq.c"
COMMANDS="$SCRIPT_DIR/streams-perq.cf"

run_case()
{
	name=$1
	flags=$2
	expected_model=$3
	output="$SCRIPT_DIR/streams-perq-$name.raw"

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --cf "$COMMANDS" \
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
		print -u2 "STREAMS per-queue $name analysis failed with status " \
		    "$status"
		print -u2 "see $output"
		return "$status"
	fi

	warnings=$(grep -c "warning:.*\\[unprotected-access\\]$" "$output")
	direct=$(grep -c \
	    "check-model-streams-perq.c:.*protection was not established" \
	    "$output")
	model=$(grep -c \
	    "streams-dki-model.c:.*protection was not established" "$output")
	all_warnings=$(grep -c "warning:" "$output")
	if (( warnings != 2 || all_warnings != 2 || direct != 2 ||
	    model != expected_model )); then
		print -u2 "Unexpected STREAMS per-queue $name findings:"
		print -u2 "  warnings=$warnings (expected 2)"
		print -u2 "  all warnings=$all_warnings (expected 2)"
		print -u2 "  direct provenance=$direct (expected 2)"
		print -u2 "  model provenance=$model (expected $expected_model)"
		print -u2 "see $output"
		return 1
	fi

	rm -f "$output"
}

run_case d-mp D_MP 4 || exit $?
run_case d-mtperq '(D_MP|D_MTPERQ)' 0 || exit $?
run_case d-mtperq-putshared '(D_MP|D_MTPERQ|D_MTPUTSHARED)' 2 || exit $?

print "New locklint D_MTPERQ put entry works."

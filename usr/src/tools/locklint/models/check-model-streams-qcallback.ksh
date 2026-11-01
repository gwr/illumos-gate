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
# Validate bound asynchronous qtimeout() and qbufcall() entry for every
# supported inner-perimeter mode and fixed queue-pair side.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qcallback.c"
COMMANDS="$SCRIPT_DIR/streams-qcallback.cf"

run_case()
{
	name=$1
	flags=$2
	side=$3
	expected_warnings=$4
	output="$SCRIPT_DIR/streams-qcallback-$name.raw"
	side_options=

	if [[ "$side" != none ]]; then
		side_options="-DMODEL_QCALLBACK_QPAIR_SIDE=$side \
		    -DQCALLBACK_QPAIR_SIDE=$side"
	fi

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none \
		    --dump-callgraph --cf "$COMMANDS" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    "-DMODEL_FLAGS=$flags" $side_options \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "STREAMS queue callback $name analysis failed " \
		    "with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	for function in qtimeout qbufcall
	do
		if ! grep -q \
		    "^  call $INPUT:.* resolved $function tu=$MODEL\$" \
		    "$output"; then
			print -u2 "STREAMS queue callback $name did not resolve " \
			    "$function"
			return 1
		fi
	done

	callbacks=$(grep -c \
	    " resolved streams_qcallback tu=$INPUT\$" "$output")
	if (( callbacks != 2 )); then
		print -u2 "STREAMS queue callback $name resolved the callback " \
		    "$callbacks times instead of twice"
		return 1
	fi

	warnings=$(grep -c "warning:" "$output")
	if (( warnings != expected_warnings )); then
		print -u2 "STREAMS queue callback $name produced $warnings " \
		    "warnings instead of $expected_warnings"
		print -u2 "see $output"
		return 1
	fi

	rm -f "$output"
}

run_case d-mp D_MP none 4 || exit $?
run_case d-mtpermod '(D_MP|D_MTPERMOD)' none 2 || exit $?
run_case d-mtpermod-shared \
    '(D_MP|D_MTPERMOD|_D_MTCBSHARED)' none 3 || exit $?
run_case d-mtperq '(D_MP|D_MTPERQ)' none 2 || exit $?
run_case d-mtperq-shared \
    '(D_MP|D_MTPERQ|_D_MTCBSHARED)' none 3 || exit $?
run_case d-mtqpair-read '(D_MP|D_MTQPAIR)' 0 2 || exit $?
run_case d-mtqpair-write '(D_MP|D_MTQPAIR)' 1 2 || exit $?
run_case d-mtqpair-shared \
    '(D_MP|D_MTQPAIR|_D_MTCBSHARED)' 1 3 || exit $?
run_case d-outer '(D_MP|D_MTOUTPERIM)' none 3 || exit $?
run_case d-mtqpair-outer \
    '(D_MP|D_MTQPAIR|D_MTOUTPERIM)' 1 3 || exit $?

print "New locklint STREAMS queue callback entry works."

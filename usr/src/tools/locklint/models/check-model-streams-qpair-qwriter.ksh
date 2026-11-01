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
# Validate immediate and deferred qwriter dispatch to exact callbacks under
# the selected D_MTQPAIR perimeter.  Deferred entry drops the scheduling
# thread's locks, immediate entry retains them, and wrong-pair protection does
# not satisfy the callback.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qpair-qwriter.c"
COMMANDS="$SCRIPT_DIR/streams-qpair-qwriter.cf"

run_case()
{
	name=$1
	flags=$2
	extra_cpp=$3
	expected_qwriter=$4
	expected_reentrance=$5
	expected_warnings=$6
	output="$SCRIPT_DIR/streams-qpair-qwriter-$name.raw"

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
		    $extra_cpp \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "STREAMS queue-pair qwriter $name analysis failed " \
		    "with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	qwriter=$(grep -c \
	    "protected member 'qwriter_data'.*\\[unprotected-access\\]$" \
	    "$output")
	caller=$(grep -c \
	    "'streams_qpair_qwriter_caller_data'.*\\[unprotected-access\\]$" \
	    "$output")
	wrong_pair=$(grep -c \
	    "'wrong_pair_data'.*\\[unprotected-access\\]$" "$output")
	reentrance=$(grep -c "\\[lock-already-held\\]$" "$output")
	bad_release=$(grep -c "\\[lock-maybe-not-held\\]$" "$output")
	unmodeled=$(grep -c "\\[unmodeled-indirect-call\\]$" "$output")
	model=$(grep -c \
	    "streams-dki-model.c:.*protection was not established" "$output")
	all_warnings=$(grep -c "warning:" "$output")
	if (( qwriter != expected_qwriter || caller != 2 ||
	    wrong_pair != 1 || reentrance != expected_reentrance ||
	    bad_release != 0 || unmodeled != 0 || model != 0 ||
	    all_warnings != expected_warnings )); then
		print -u2 "Unexpected STREAMS queue-pair qwriter $name findings:"
		print -u2 "  unprotected qwriter data=$qwriter " \
		    "(expected $expected_qwriter)"
		print -u2 "  missing scheduling lock=$caller (expected 2)"
		print -u2 "  wrong-pair protection=$wrong_pair (expected 1)"
		print -u2 "  immediate reentrance=$reentrance " \
		    "(expected $expected_reentrance)"
		print -u2 "  bad releases=$bad_release (expected 0)"
		print -u2 "  unmodeled callbacks=$unmodeled (expected 0)"
		print -u2 "  model provenance=$model (expected 0)"
		print -u2 "  all warnings=$all_warnings " \
		    "(expected $expected_warnings)"
		print -u2 "see $output"
		return 1
	fi

	rm -f "$output"
}

run_case d-mp D_MP "" 4 0 7 || exit $?
run_case d-mtqpair '(D_MP|D_MTQPAIR)' \
    -DMODEL_QPAIR_REENTRANCE 0 1 4 || exit $?

print "New locklint D_MTQPAIR qwriter immediate and deferred dispatch works."

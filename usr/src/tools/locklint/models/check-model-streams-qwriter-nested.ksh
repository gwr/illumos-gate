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
# Check nested inner qwriter ownership in each supported perimeter scope.
# Exact diagnostic controls also reject unintended nested deferral, lost
# writer ownership, recursive acquisition, and bad release.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
INPUT="$SCRIPT_DIR/check-model-streams-qwriter-nested.c"
MODEL="$SCRIPT_DIR/streams-dki-model.c"

run_case()
{
	name=$1
	flags=$2
	extra_cpp=$3
	output="$SCRIPT_DIR/streams-qwriter-nested-$name.raw"

	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none \
		    --cf "$SCRIPT_DIR/streams-qwriter-nested.cf" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    "-DMODEL_FLAGS=$flags" $extra_cpp \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "Nested qwriter $name analysis failed with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	caller=$(grep -c "'caller_data'.*\[unprotected-access\]\$" "$output")
	after=$(grep -c "'after_write'.*\[conditional-protection\]\$" "$output")
	exit_count=$(grep -c "'after_exit'.*\[unprotected-access\]\$" "$output")
	warnings=$(grep -c 'warning:' "$output")
	if (( caller != 1 || after != 1 || exit_count != 1 || warnings != 3 )); then
		print -u2 "Nested qwriter $name: caller mutex=$caller, " \
		    "conditional write=$after, released perimeter=$exit_count " \
		    "(expected 1 each), warnings=$warnings (expected 3)"
		print -u2 "see $output"
		return 1
	fi
	rm -f "$output"
}

run_case permod '(D_MP|D_MTPERMOD|D_MTPUTSHARED)' '' || exit $?
run_case perq '(D_MP|D_MTPERQ|D_MTPUTSHARED)' '' || exit $?
run_case qpair '(D_MP|D_MTQPAIR|D_MTPUTSHARED)' \
    '-DMODEL_QWRITER_QPAIR_SIDE=1' || exit $?

print "New locklint nested inner qwriter retains callback locks and ownership."

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
# Check that putnext adds no perimeter and preserves caller ownership.
# Keep automatic callback roots disabled so only the two explicit callers
# contribute states to the boundary callback.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-putnext.c"

run_case()
{
	name=$1
	flags=$2
	output="$SCRIPT_DIR/streams-putnext-$name.raw"

	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none --dump-callgraph \
		    --cf "$SCRIPT_DIR/streams-putnext.cf" \
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
		print -u2 "STREAMS putnext $name analysis failed with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	calls=$(grep -c \
	    "^  call $INPUT:.* resolved putnext tu=$MODEL\$" "$output")
	warnings=$(grep -c 'warning:' "$output")
	if (( calls != 2 || warnings != 3 )); then
		print -u2 "STREAMS putnext $name: calls=$calls (expected 2), " \
		    "warnings=$warnings (expected 3); see $output"
		return 1
	fi
	for datum in perimeter rwlock mutex
	do
		count=$(grep -c \
		    "'streams_putnext_${datum}_data'.*\[unprotected-access\]\$" \
		    "$output")
		if (( count != 1 )); then
			print -u2 "STREAMS putnext $name: $datum findings=$count " \
			    "(expected 1); see $output"
			return 1
		fi
	done

	rm -f "$output"
}

run_case inner '(D_MP|D_MTPERMOD)' || exit $?
run_case outer '(D_MP|D_MTOUTPERIM)' || exit $?

print "New locklint putnext preserves caller locks without adding a perimeter."

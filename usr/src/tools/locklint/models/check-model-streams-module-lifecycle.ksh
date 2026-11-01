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
# Validate queue-pair callback reachability between qprocson() and
# qprocsoff(), including fresh asynchronous lock state.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-module-lifecycle.c"
COMMANDS="$SCRIPT_DIR/streams-module-lifecycle.cf"

run_case()
{
	name=$1
	enabled=$2
	reachable=$3
	expected_warnings=$4
	output="$SCRIPT_DIR/streams-module-lifecycle-$name.raw"

	rm -f "$output"
	(
		cd "$MODULE_DIR" || exit 1
		"$LOCKLINT" --compat=osll --root-discovery=none \
		    --dump-callgraph --cf "$COMMANDS" \
		    -D__sun -m64 -Ui386 -U__i386 -O2 \
		    -D_ASM_INLINES -std=gnu99 \
		    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
		    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
		    -DMODEL_MODULE_ENTRY -DMODEL_MODULE_LIFECYCLE \
		    "-DLIFECYCLE_ENABLE=$enabled" \
		    '-DMODEL_FLAGS=(D_MP|D_MTQPAIR)' \
		    -I../../intel -nostdinc -I../../common \
		    "$INPUT" "$MODEL"
	) >"$output" 2>&1
	status=$?
	if (( status != 0 )); then
		print -u2 "STREAMS module lifecycle $name analysis failed " \
		    "with status $status"
		print -u2 "see $output"
		return "$status"
	fi

	for callback in \
	    streams_lifecycle_read_put streams_lifecycle_write_put \
	    streams_lifecycle_read_service streams_lifecycle_write_service
	do
		if ! grep -q \
		    "^function $callback .* reachable=$reachable\$" "$output"; then
			print -u2 "STREAMS module lifecycle $name has unexpected " \
			    "reachability for $callback"
			print -u2 "see $output"
			return 1
		fi
	done

	if (( enabled != 0 )); then
		enabled_calls=$(grep -c \
		    " resolved locklint_module_enabled tu=$MODEL\$" "$output")
		if (( enabled_calls != 1 )); then
			print -u2 "STREAMS module lifecycle $name has " \
			    "$enabled_calls enabled-pair calls instead of one"
			return 1
		fi
	fi

	warnings=$(grep -c "warning:" "$output")
	if (( warnings != expected_warnings )); then
		print -u2 "STREAMS module lifecycle $name produced $warnings " \
		    "warnings instead of $expected_warnings"
		print -u2 "see $output"
		return 1
	fi
	if grep -q "\\[lock-held-during-wait\\]\$" "$output"; then
		print -u2 "STREAMS module lifecycle $name reported its " \
		    "framework perimeter across qprocsoff"
		return 1
	fi

	rm -f "$output"
}

run_case enabled 1 yes 2 || exit $?
run_case disabled 0 no 0 || exit $?

print "New locklint STREAMS queue lifecycle gating works."

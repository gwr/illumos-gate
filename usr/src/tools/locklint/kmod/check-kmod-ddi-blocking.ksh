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
# Validate current delay() blocking behavior in the common DDI/DKI model.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
INPUT="$SCRIPT_DIR/check-kmod-ddi-blocking.c"
OUTPUT="$WORK_DIR/ddi-blocking.raw"

mkdir -p "$WORK_DIR" || exit 1
rm -f "$OUTPUT"

cd "$MODULE_DIR" || exit 1
"$LOCKLINT" --compat=osll --dump-callgraph \
    --cf "$SCRIPT_DIR/ddi-blocking.cf" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -I../../intel -nostdinc -I../../common \
    "$INPUT" "$MODEL" >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "DDI blocking model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

if ! grep -q \
    "^  call $INPUT:.* resolved delay tu=$MODEL\$" "$OUTPUT"; then
	print -u2 "DDI blocking input did not resolve delay"
	exit 1
fi

wait_count=$(grep -c \
    "\\[lock-held-during-wait\\]\$" "$OUTPUT")
if (( wait_count != 1 )); then
	print -u2 "DDI delay produced $wait_count held-during-wait diagnostics " \
	    "instead of one"
	print -u2 "see $OUTPUT"
	exit 1
fi

if ! grep -q "lock is held on a path through call to 'delay'\$" "$OUTPUT"; then
	print -u2 "DDI delay diagnostic lacks caller provenance"
	exit 1
fi

print "New locklint delay model preserves blocking lock state."

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
# Run the new locklint over the complete wc kernel module using OSLL-compatible
# preprocessing.  The frontend arguments are the command emitted by cw for the
# illumos Smatch shadow, minus Smatch checker controls and object-output
# arguments.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/wc"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-wc.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-wc.out"

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "wc module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi

mkdir -p "$WORK_DIR" || exit 1

run_locklint()
{
	"$LOCKLINT" "$@" \
	    -fident -finline -fno-inline-functions -fno-builtin -fno-asm \
	    -fdiagnostics-show-option -nodefaultlibs -D__sun -m64 \
	    -mtune=opteron -Ui386 -U__i386 -fno-strict-aliasing \
	    -fno-unit-at-a-time -fno-optimize-sibling-calls -O2 \
	    -D_ASM_INLINES -ffreestanding -mno-red-zone -mno-mmx -mno-sse \
	    -msave-args -Wall -Wextra -g -gdwarf-4 -gstrict-dwarf \
	    -std=gnu99 -msave-args -Werror \
	    -Wno-missing-braces -Wno-sign-compare -Wno-unused-parameter \
	    -Wno-missing-field-initializers -Winline \
	    -Wno-maybe-uninitialized -fno-inline-small-functions \
	    -fno-inline-functions-called-once -fno-ipa-cp -fno-ipa-icf \
	    -fno-clone-functions -fno-reorder-functions \
	    -fno-reorder-blocks-and-partition \
	    -fno-aggressive-loop-optimizations \
	    --param=max-inline-insns-single=450 -fno-shrink-wrap \
	    -mindirect-branch=thunk-extern -mindirect-branch-register \
	    -fno-asynchronous-unwind-tables -fstack-protector-strong \
	    -D_KERNEL -ffreestanding -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I../../intel -nostdinc -I../../common \
	    -mcmodel=kernel \
	    ../../common/io/wscons.c ../../common/io/vcons.c \
	    "$SCRIPT_DIR/check-kmod-wc-sentinel.c"
}

print "Running new locklint over wc"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/wc.cf" \
    >"$ANALYZE_RAW" 2>&1
status=$?
if [[ "$status" -ne 0 ]]; then
	print -u2 "new locklint wc analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

if ! grep -q '^function wcopen tu=../../common/io/wscons.c ' \
    "$ANALYZE_RAW" ||
    ! grep -q '^function vc_avl_compare tu=../../common/io/vcons.c ' \
    "$ANALYZE_RAW"; then
	print -u2 "new locklint wc analysis omitted expected module functions"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

sentinel_count=$(grep -c \
    'check-kmod-wc-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if [[ "$sentinel_count" -ne 1 ]]; then
	print -u2 "new locklint wc analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

awk '/warning: locklint:/ &&
    $0 !~ /check-kmod-wc-sentinel.c:/ { print }' \
    "$ANALYZE_RAW" >"$OUTPUT" || exit 1
if cmp -s "$REFERENCE" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint wc semantic output matches check-kmod-wc.ref."
else
	print -u2 "New locklint wc semantic output differs from " \
	    "check-kmod-wc.ref:"
	diff -u "$REFERENCE" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

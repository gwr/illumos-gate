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
# Validate optional immediate STREAMS dispatch plus independent callback entry
# in the common DDI/DKI representative model.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
INPUT="$SCRIPT_DIR/check-kmod-streams-dispatch.c"
OUTPUT="$SCRIPT_DIR/streams-dispatch.raw"

rm -f "$OUTPUT"

cd "$MODULE_DIR" || exit 1
"$LOCKLINT" --compat=osll --dump-callgraph \
    --cf "$SCRIPT_DIR/streams-dispatch.cf" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -I../../intel -nostdinc -I../../common \
    "$INPUT" "$MODEL" >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "STREAMS dispatch model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

for function in qwriter putnext
do
	if ! grep -q \
	    "^  call $INPUT:.* resolved ${function} tu=$MODEL\$" "$OUTPUT"; then
		print -u2 "STREAMS dispatch model did not resolve $function"
		exit 1
	fi
done

for function in streams_qwriter_callback streams_putnext_callback
do
	if ! awk -v target="$function" '
	    /^function / {
		in_function = ($2 == target)
	    }
	    in_function && /^  root function-pointer-escape$/ {
		found = 1
	    }
	    END {
		exit !found
	    }
	' "$OUTPUT"; then
		print -u2 "$function is not an independent callback root"
		exit 1
	fi
done

for lock in qwriter_effect_lock putnext_effect_lock
do
	if ! grep -q \
	    "lock '$lock' may not be held.*\\[lock-maybe-not-held\\]$" \
	    "$OUTPUT"; then
		print -u2 "$lock did not retain optional direct callback effects"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done

print "New locklint STREAMS representatives preserve optional dispatch."

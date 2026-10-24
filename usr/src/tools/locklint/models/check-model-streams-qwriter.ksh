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
# Validate immediate and independently scheduled qwriter callback entry under
# the selected single-module inner or outer perimeter.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qwriter.c"
COMMANDS="$SCRIPT_DIR/streams-qwriter.cf"
OUTPUT="$SCRIPT_DIR/streams-qwriter.raw"

rm -f "$OUTPUT"
(
	cd "$MODULE_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none --dump-callgraph \
	    --cf "$COMMANDS" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I../../intel -nostdinc -I../../common \
	    "$INPUT" "$MODEL"
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "STREAMS qwriter model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

if ! grep -q \
    "^  call $INPUT:.* resolved qwriter tu=$MODEL\$" "$OUTPUT"; then
	print -u2 "STREAMS qwriter model did not resolve qwriter"
	exit 1
fi
if grep -q "asserted-lock-requirement" "$OUTPUT"; then
	print -u2 "STREAMS qwriter model has a callback entry-mode mismatch"
	print -u2 "see $OUTPUT"
	exit 1
fi

for data in inner_data outer_data forward_data
do
	if grep -q "'$data'.*\\[unprotected-access\\]$" "$OUTPUT"; then
		print -u2 "STREAMS qwriter did not protect $data"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done

unknown=$(grep -c "'unknown_data'.*\\[unprotected-access\\]$" "$OUTPUT")
if (( unknown != 2 )); then
	print -u2 "Unexpected STREAMS qwriter unknown-selector findings: " \
	    "$unknown (expected 2)"
	print -u2 "see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint qwriter perimeter entry works."

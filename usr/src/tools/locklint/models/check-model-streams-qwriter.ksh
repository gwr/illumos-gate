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
	    --dump-annotations \
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
schedule=$(grep -c \
    "'scheduling_data'.*\\[unprotected-access\\]$" "$OUTPUT")
annotations=$(grep -c "ASYNC_ENTRY locklint_qwriter_deferred" "$OUTPUT")
if (( unknown != 2 || schedule != 4 || annotations != 1 )); then
	print -u2 "Unexpected STREAMS qwriter unknown-selector findings: " \
	    "$unknown (expected 2)"
	print -u2 "  missing scheduling lock=$schedule (expected 4)"
	print -u2 "  async annotations=$annotations (expected 1)"
	print -u2 "see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"

# Exercise legal shared framework entry separately from the dispatch fixtures.
OUTPUT="$SCRIPT_DIR/streams-qwriter-shared-entry.raw"
(
	cd "$MODULE_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none --dump-callgraph \
	    --cf "$SCRIPT_DIR/streams-qwriter-shared-entry.cf" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    '-DMODEL_FLAGS=(D_MP|D_MTPERMOD|D_MTPUTSHARED)' \
	    -I../../intel -nostdinc -I../../common \
	    "$INPUT" "$MODEL"
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "STREAMS shared-entry qwriter analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

before=$(grep -c "'shared_before_data'.*\[unprotected-access\]\$" "$OUTPUT")
if ! grep -q \
    "^  call $INPUT:.* resolved locklint_service tu=$MODEL\$" "$OUTPUT"; then
	print -u2 "STREAMS shared-entry qwriter did not resolve service entry"
	exit 1
fi
after=$(grep -c "'shared_after_write'.*\[conditional-protection\]\$" "$OUTPUT")
released=$(grep -c "'shared_after_exit'.*\[unprotected-access\]\$" "$OUTPUT")
schedule=$(grep -c "'scheduling_data'.*\[unprotected-access\]\$" "$OUTPUT")
warnings=$(grep -c 'warning:' "$OUTPUT")
if (( before != 1 || after != 1 || released != 1 || schedule != 1 ||
    warnings != 4 )); then
	print -u2 "Unexpected shared-entry qwriter findings:"
	print -u2 "  reader-only entry=$before (expected 1)"
	print -u2 "  conditional post-call write=$after (expected 1)"
	print -u2 "  released framework lock=$released (expected 1)"
	print -u2 "  missing deferred caller mutex=$schedule (expected 1)"
	print -u2 "  all warnings=$warnings (expected 4); see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint qwriter immediate and deferred perimeter entry works."

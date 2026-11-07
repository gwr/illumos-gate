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
# Check outer reader restoration and independent deferred writer entry.
# Caller rwlock effects ensure both scheduling alternatives remain visible.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-qwriter-outer.c"
OUTPUT="$SCRIPT_DIR/streams-qwriter-outer.raw"

(
	cd "$MODULE_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none \
	    --cf "$SCRIPT_DIR/streams-qwriter-outer.cf" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    '-DMODEL_FLAGS=(D_MP|D_MTOUTPERIM)' \
	    -I../../intel -nostdinc -I../../common \
	    "$INPUT" "$MODEL"
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "Outer qwriter analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

for datum in before_write after_write after_exit caller_data
do
	count=$(grep -c "'$datum'.*\[unprotected-access\]\$" "$OUTPUT")
	if (( count != 1 )); then
		print -u2 "Outer qwriter: $datum findings=$count (expected 1)"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done
effects=$(grep -c "'caller_after'.*\[conditional-protection\]\$" "$OUTPUT")
warnings=$(grep -c 'warning:' "$OUTPUT")
if (( effects != 1 || warnings != 5 )); then
	print -u2 "Outer qwriter: conditional caller effects=$effects " \
	    "(expected 1), warnings=$warnings (expected 5); see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint outer qwriter restores the enclosing reader claim."

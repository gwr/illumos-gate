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
# Validate root-like async entry with ordinary call argument bindings.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
LOCKLINT=${LOCKLINT:-"$SCRIPT_DIR/../locklint"}
OUTPUT="$SCRIPT_DIR/async-entry.raw"

rm -f "$OUTPUT"
(
	cd "$SCRIPT_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none \
	    --cf async-entry.cf --dump-annotations \
	    async-entry.c async-entry-second.c
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "Async-entry analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

first_missing=$(grep -c \
    "'async_entry_caller_data'.*\\[unprotected-access\\]$" "$OUTPUT")
second_missing=$(grep -c \
    "'async_entry_second_caller_data'.*\\[unprotected-access\\]$" "$OUTPUT")
object_missing=$(grep -c \
    "async_entry_state::data.*\\[unprotected-access\\]$" "$OUTPUT")
returned_lock=$(grep -c "\\[lock-already-held\\]$" "$OUTPUT")
unmodeled=$(grep -c "\\[unmodeled-indirect-call\\]$" "$OUTPUT")
annotations=$(grep -c "ASYNC_ENTRY async_worker" "$OUTPUT")
warnings=$(grep -c "warning:" "$OUTPUT")

if (( first_missing != 1 || second_missing != 1 ||
    object_missing != 0 || returned_lock != 0 || unmodeled != 0 ||
    annotations != 2 || warnings != 2 )); then
	print -u2 "Unexpected async-entry findings:"
	print -u2 "  first missing caller lock=$first_missing (expected 1)"
	print -u2 "  second missing caller lock=$second_missing (expected 1)"
	print -u2 "  object protection failures=$object_missing (expected 0)"
	print -u2 "  returned async locks=$returned_lock (expected 0)"
	print -u2 "  unmodeled callbacks=$unmodeled (expected 0)"
	print -u2 "  annotations=$annotations (expected 2)"
	print -u2 "  warnings=$warnings (expected 2)"
	print -u2 "see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint async entry preserves bindings with fresh lock state."

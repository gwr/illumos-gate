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
# Check ordinary predicate branches, keeping real negative controls visible
# and requiring remote-owner and unresolved queries to retain both alternatives.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
LOCKLINT=${LOCKLINT:-"$SCRIPT_DIR/../locklint"}
OUTPUT=rwlock-predicate.out

cd "$SCRIPT_DIR" || exit 1
"$LOCKLINT" rwlock-predicate.c >"$OUTPUT" 2>&1 || exit $?

for datum in unheld reader other mixed saved unresolved_true unresolved_false \
    remote_reader remote_reader_false remote_lock remote_lock_false
do
	count=$(grep -c "'${datum}_data'.*\[unprotected-access\]\$" "$OUTPUT")
	if (( count != 1 )); then
		print -u2 "Rwlock predicate: $datum findings=$count (expected 1)"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done

warnings=$(grep -c 'warning:' "$OUTPUT")
if (( warnings != 11 )); then
	print -u2 "Rwlock predicate: warnings=$warnings (expected 11); see $OUTPUT"
	exit 1
fi

print "New locklint rwlock predicates select feasible branches without effects."

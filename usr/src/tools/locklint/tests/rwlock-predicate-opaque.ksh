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
# Check opaque writer facts and exact negative controls.  Extra diagnostics
# catch invented acquisitions, requirements, and invalid exemption state.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
LOCKLINT=${LOCKLINT:-"$SCRIPT_DIR/../locklint"}
OUTPUT=rwlock-predicate-opaque.out

cd "$SCRIPT_DIR" || exit 1
"$LOCKLINT" rwlock-predicate-opaque.c >"$OUTPUT" 2>&1 || exit $?

for datum in other false remote released
do
	count=$(grep -c "'${datum}_data'.*\\[unprotected-access\\]\$" "$OUTPUT")
	if (( count != 1 )); then
		print -u2 "Opaque predicate: $datum findings=$count (expected 1)"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done
recursive=$(grep -c '\[lock-already-held\]$' "$OUTPUT")
if (( recursive != 1 )); then
	print -u2 "Opaque predicate: recursive findings=$recursive (expected 1)"
	print -u2 "see $OUTPUT"
	exit 1
fi
warnings=$(grep -c 'warning:' "$OUTPUT")
if (( warnings != 5 )); then
	print -u2 "Opaque predicate: warnings=$warnings (expected 5); see $OUTPUT"
	exit 1
fi
print "New locklint opaque writer predicates establish observed ownership."

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
# Check allocated-object visibility diagnostics and audit a valid read-only
# lifecycle separately from the post-publication and reassignment controls.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
LOCKLINT=${LOCKLINT:-"$SCRIPT_DIR/../locklint"}
DIAGNOSTICS=visibility-allocated.out
PROTECTION=visibility-allocated-protection.out
UNPROTECTED=visibility-allocated-unprotected.out
AUDIT_DIAGNOSTICS=visibility-allocated-audit.out

fail()
{
	print -u2 "Allocated object visibility: $*"
	exit 1
}

cd "$SCRIPT_DIR" || exit 1
"$LOCKLINT" visibility-allocated.c >"$DIAGNOSTICS" 2>&1 ||
    fail "diagnostic analysis failed; see $DIAGNOSTICS"

private=$(grep -c 'visibility-allocated.c:39:14:.*\[read-only-' "$DIAGNOSTICS")
published=$(grep -c \
    "visibility-allocated.c:41:14: warning: read-only data 'value' modified while visible to competing threads \[read-only-visible\]" \
    "$DIAGNOSTICS")
reassigned=$(grep -c \
    "visibility-allocated.c:52:14: warning: read-only data 'value' modified while visible to competing threads \[read-only-visible\]" \
    "$DIAGNOSTICS")
warnings=$(grep -c 'warning:' "$DIAGNOSTICS")
if (( private != 0 || published != 1 || reassigned != 1 || warnings != 2 )); then
	fail "unexpected initialization/publication findings; see $DIAGNOSTICS"
fi

"$LOCKLINT" --root-discovery=none --cf visibility-allocated.cf \
    --no-diagnostics --audit-protection="$PROTECTION" \
    visibility-allocated.c >"$AUDIT_DIAGNOSTICS" 2>&1 ||
    fail "protection analysis failed; see $AUDIT_DIAGNOSTICS"

rows=$(awk -F '\t' '
    $1 == "allocated_state::value" && $2 == "read-only" &&
    $3 == "read-only" && $4 == "-" { count++ }
    END { print count + 0 }
' "$PROTECTION")
if (( rows != 1 )); then
	fail "expected read-only access and policy without notes; see $PROTECTION"
fi

"$LOCKLINT" --root-discovery=none --cf visibility-allocated.cf \
    --no-diagnostics --audit-unprotected="$UNPROTECTED" \
    visibility-allocated.c >>"$AUDIT_DIAGNOSTICS" 2>&1 ||
    fail "unprotected analysis failed; see $AUDIT_DIAGNOSTICS"

lines=$(wc -l <"$UNPROTECTED")
if (( lines != 1 )) || ! grep -q '^Unprotected audit:' "$UNPROTECTED"; then
	fail "private initialization has unprotected audit entries; see $UNPROTECTED"
fi

print "New locklint allocated-object visibility diagnostics and audits match."

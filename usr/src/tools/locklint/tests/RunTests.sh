#!/bin/sh
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
# Copyright 2026 Gordon W. Ross
#

LOCKLINT=../locklint
failures=0

fail()
{
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

run_capture()
{
	name=$1
	output=$2
	shift 2

	echo "test: $name"
	if ! "$@" > "$output" 2>&1; then
		fail "$name: command failed"
	fi
}

run_failure()
{
	name=$1
	output=$2
	shift 2

	echo "test: $name"
	if "$@" > "$output" 2>&1; then
		fail "$name: command unexpectedly succeeded"
	fi
}

compare()
{
	name=$1
	reference=$2
	output=$3

	if ! diff -u "$reference" "$output"; then
		fail "$name: output differs"
	fi
}

require_match()
{
	name=$1
	pattern=$2
	output=$3

	if ! grep -q "$pattern" "$output"; then
		fail "$name: missing '$pattern'"
	fi
}

reject_match()
{
	name=$1
	pattern=$2
	output=$3

	if grep -q "$pattern" "$output"; then
		fail "$name: unexpected '$pattern'"
	fi
}

#
# Verify initial Sparse parsing, lowering, and source access identity.
#
run_capture "parsed smoke" parsed.out "$LOCKLINT" --dump-parsed smoke.c
require_match "parsed smoke" smoke parsed.out

run_capture "linearized smoke" linearized.out \
    "$LOCKLINT" --dump-linearized smoke.c
if ! grep -Eq 'load|store' linearized.out; then
	fail "linearized smoke: missing load or store"
fi

run_capture "access smoke" accesses.out "$LOCKLINT" --dump-accesses smoke.c
require_match "access smoke" 'load arg.nested.value ' accesses.out
require_match "access smoke" 'store global_smoke.head ' accesses.out
require_match "access smoke" 'store local.values ' accesses.out
require_match "access smoke" 'load arg.next.value ' accesses.out
require_match "access smoke" 'store static_value ' accesses.out

#
# Verify ordered memory, call, acquisition, and release events.
#
run_capture "events" events.out "$LOCKLINT" --dump-events events.c
compare "events" events.ref events.out

#
# Verify preprocessing-time annotation capture and initial name resolution.
#
run_capture "annotations" annotations.out \
    "$LOCKLINT" --dump-annotations annotations.c
compare "annotations" annotations.ref annotations.out

#
# Verify intraprocedural lock state across branches, loops, and returns.
#
run_capture "check" check.out "$LOCKLINT" --check-locks check.c
compare "check" check.ref check.out

#
# Verify lock conditions and lock effects propagated through direct calls.
#
for test in calls global-conditions effects
do
	run_capture "$test" "$test.out" "$LOCKLINT" --check-locks "$test.c"
	compare "$test" "$test.ref" "$test.out"
done

#
# Verify calls, lock conditions, and effects across translation units.
#
run_capture "cross translation unit" cross.out "$LOCKLINT" --check-locks \
    cross-caller.c cross-callee.c
compare "cross translation unit" cross.ref cross.out

#
# Verify external object and member identity across translation units.
#
run_capture "external objects" external-objects.raw \
    "$LOCKLINT" --check-locks external-objects-caller.c \
    external-objects-callee.c
sed 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
    external-objects.raw > external-objects.out
compare "external objects" external-objects.ref external-objects.out

#
# Verify annotations from a command-line forced include retain provenance.
#
run_capture "forced include" forced-include.raw \
    "$LOCKLINT" --check-locks -include forced-include.h forced-include.c
sed 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
    forced-include.raw > forced-include.out
compare "forced include" forced-include.ref forced-include.out

run_failure "forced include multiple inputs" forced-include-multiple.out \
    "$LOCKLINT" --check-locks -include forced-include.h forced-include.c \
    forced-include-second.c
require_match "forced include multiple inputs" \
    "multiple inputs with initialization-time internal declarations" \
    forced-include-multiple.out

#
# Verify assertion predicates refine state without exposing macro bodies.
#
run_capture "assertions" assertions.out \
    "$LOCKLINT" --check-locks assertions.c
compare "assertions" assertions.ref assertions.out

run_capture "assertion events" assertion-events.out \
    "$LOCKLINT" --check-locks --dump-events assertions.c
require_match "assertion events" 'CALL mutex_owned' assertion-events.out
reject_match "assertion events" 'CALL assfail' assertion-events.out

#
# Verify user-level mutex operations use the common mutex analysis.
#
run_capture "user mutex" user-mutex.out \
    "$LOCKLINT" --check-locks user-mutex.c
compare "user mutex" user-mutex.ref user-mutex.out

#
# Verify unresolved mutex annotation names produce diagnostics.
#
echo "test: annotation errors"
if "$LOCKLINT" --dump-annotations annotation-errors.c \
    > annotation-errors.out 2>&1; then
	fail "annotation errors: command unexpectedly succeeded"
fi
require_match "annotation errors" \
    "unresolved annotation name 'missing_lock'" annotation-errors.out
require_match "annotation errors" \
    "unresolved annotation name 'error_object.missing_lock'" \
    annotation-errors.out
require_match "annotation errors" \
    "unresolved annotation name 'error_state::missing_value'" \
    annotation-errors.out

#
# Verify complete mutex annotation names and recursive expansion.
#
run_capture "annotation names" annotation-names.raw \
    "$LOCKLINT" --check-locks annotation-names.c
sed 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
    annotation-names.raw > annotation-names.out
compare "annotation names" annotation-names.ref annotation-names.out

#
# Verify type-scoped annotations through anonymous aggregate embedding.
#
run_capture "anonymous embedding" anonymous-embedding.raw \
    "$LOCKLINT" --check-locks anonymous-embedding.c
sed 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
    anonymous-embedding.raw > anonymous-embedding.out
compare "anonymous embedding" anonymous-embedding.ref \
    anonymous-embedding.out

#
# Verify later protection declarations report override provenance.
#
run_capture "annotation name dump" annotation-names-dump.out \
    "$LOCKLINT" --dump-annotations annotation-names.c
require_match "annotation name dump" \
    'replaced by annotation-names.c:61:1' annotation-names-dump.out
require_match "annotation name dump" \
    'replaces annotation-names.c:59:1' annotation-names-dump.out

#
# Final report
#
if [ "$failures" -ne 0 ]; then
	echo "$failures locklint test(s) failed" >&2
	exit 1
fi

echo "All locklint tests passed"

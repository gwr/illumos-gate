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

	capture_failed=0
	echo "test: $name"
	if ! "$@" > "$output" 2>&1; then
		capture_failed=1
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
		return 1
	fi
	return 0
}

compare_no_columns()
{
	name=$1
	reference=$2
	raw_output=$3
	output=$4

	sed 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
	    "$raw_output" > "$output"
	if compare "$name" "$reference" "$output" &&
	    [ "$capture_failed" -eq 0 ]; then
		rm -f "$raw_output"
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

require_empty()
{
	name=$1
	output=$2

	if [ -s "$output" ]; then
		fail "$name: unexpected output"
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

run_capture "declared effect annotations" declared-effect-annotations.out \
    "$LOCKLINT" --dump-annotations declared-effect-annotations.c
compare "declared effect annotations" declared-effect-annotations.ref \
    declared-effect-annotations.out

run_failure "declared effect errors" declared-effect-errors.out \
    "$LOCKLINT" --dump-annotations declared-effect-errors.c
require_match "declared mutex effect expression" \
    "MUTEX_ACQUIRED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-errors.out
require_match "declared release effect expression" \
    "LOCK_RELEASED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-errors.out

run_failure "declared effect check errors" declared-effect-check-errors.out \
    "$LOCKLINT" --check-locks declared-effect-errors.c
require_match "checked declared mutex effect expression" \
    "MUTEX_ACQUIRED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out
require_match "checked declared release effect expression" \
    "LOCK_RELEASED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out

#
# Verify LOCK_ORDER name resolution and optional comma separators.
#
run_capture "lock order annotations" lock-order-annotations.out \
    "$LOCKLINT" --dump-annotations lock-order-annotations.c
compare "lock order annotations" lock-order-annotations.ref \
    lock-order-annotations.out

run_failure "lock order annotation errors" lock-order-errors.out \
    "$LOCKLINT" --dump-annotations lock-order-errors.c
require_match "lock order minimum names" \
    "lock-order-errors.c:32:.*LOCK_ORDER requires at least two lock names" \
    lock-order-errors.out
require_match "lock order leading comma" \
    "lock-order-errors.c:33:.*unexpected comma in LOCK_ORDER" \
    lock-order-errors.out
require_match "lock order repeated comma" \
    "lock-order-errors.c:34:.*unexpected comma in LOCK_ORDER" \
    lock-order-errors.out
require_match "lock order trailing comma" \
    "lock-order-errors.c:35:.*trailing comma in LOCK_ORDER" \
    lock-order-errors.out

run_capture "lock order declaration cycle" lock-order-cycle.out \
    "$LOCKLINT" --check-locks lock-order-cycle.c
compare "lock order declaration cycle" lock-order-cycle.ref \
    lock-order-cycle.out

run_capture "local lock order" lock-order.out \
    "$LOCKLINT" --check-locks lock-order.c
compare "local lock order" lock-order.ref lock-order.out

run_capture "observed lock order" lock-order-observed.out \
    "$LOCKLINT" --check-locks lock-order-observed.c
compare "observed lock order" lock-order-observed.ref \
    lock-order-observed.out

#
# Verify independent protection mechanism, unlocked-read, and read-only
# policy dimensions.
#
run_capture "data policy annotations" data-policy-annotations.out \
    "$LOCKLINT" --dump-annotations data-policy.c
compare "data policy annotations" data-policy-annotations.ref \
    data-policy-annotations.out

run_capture "data policy checks" data-policy.out \
    "$LOCKLINT" --check-locks data-policy.c
compare "data policy checks" data-policy.ref data-policy.out

run_capture "rwlock annotations" rwlock-annotations.out \
    "$LOCKLINT" --dump-annotations rwlock.c
compare "rwlock annotations" rwlock-annotations.ref \
    rwlock-annotations.out

run_capture "user rwlock state" rwlock-user.out \
    "$LOCKLINT" --check-locks rwlock.c
compare "user rwlock state" rwlock-user.ref rwlock-user.out

run_capture "kernel rwlock state" rwlock-kernel.out \
    "$LOCKLINT" -D_KERNEL --check-locks rwlock.c
compare "kernel rwlock state" rwlock-kernel.ref rwlock-kernel.out

run_capture "user rwlock events" rwlock-events-user.out \
    "$LOCKLINT" --dump-events rwlock.c
require_match "user rwlock reader event" "ACQUIRE-READ state.lock" \
    rwlock-events-user.out
require_match "user rwlock writer event" "ACQUIRE-WRITE state.lock" \
    rwlock-events-user.out
require_match "user rwlock release event" "RELEASE state.lock" \
    rwlock-events-user.out

run_capture "kernel rwlock events" rwlock-events-kernel.out \
    "$LOCKLINT" -D_KERNEL --dump-events rwlock.c
require_match "kernel rwlock reader event" "ACQUIRE-READ state.lock" \
    rwlock-events-kernel.out
require_match "kernel rwlock writer event" "ACQUIRE-WRITE state.lock" \
    rwlock-events-kernel.out
require_match "kernel rwlock release event" "RELEASE state.lock" \
    rwlock-events-kernel.out
require_match "rwlock unknown-mode call" "CALL rw_enter" \
    rwlock-events-kernel.out

run_capture "user rwlock calls" rwlock-calls-user.out \
    "$LOCKLINT" --check-locks rwlock-calls.c
compare "user rwlock calls" rwlock-calls.ref rwlock-calls-user.out

run_capture "kernel rwlock calls" rwlock-calls-kernel.out \
    "$LOCKLINT" -D_KERNEL --check-locks rwlock-calls.c
compare "kernel rwlock calls" rwlock-calls.ref rwlock-calls-kernel.out

#
# Verify structure-valued mutexes retain whole-object lock identity.
#
run_capture "structure-valued locks" struct-lock.out \
    "$LOCKLINT" --check-locks struct-lock.c
compare "structure-valued locks" struct-lock.ref struct-lock.out

#
# Verify executable annotations survive as ordered tagged contexts.
#
run_capture "execution markers" visibility-linearized.out \
    "$LOCKLINT" --dump-annotations --dump-linearized visibility.c
grep 'context     ' visibility-linearized.out > visibility-markers.out
compare "execution markers" visibility-markers.ref visibility-markers.out

run_capture "local exposure state" visibility-state.out \
    "$LOCKLINT" --check-locks visibility.c
compare "local exposure state" visibility-state.ref visibility-state.out

#
# Verify intraprocedural lock state across branches, loops, and returns.
#
run_capture "check" check.out "$LOCKLINT" --check-locks check.c
compare "check" check.ref check.out

#
# Verify lock conditions and lock effects propagated through direct calls.
#
for test in calls global-conditions effects visibility-calls
do
	run_capture "$test" "$test.out" "$LOCKLINT" --check-locks "$test.c"
	compare "$test" "$test.ref" "$test.out"
done

run_capture "declared effects" declared-effects.raw \
    "$LOCKLINT" --check-locks declared-effects.c
compare_no_columns "declared effects" declared-effects.ref \
    declared-effects.raw declared-effects.out

run_capture "absolute declared effects" declared-effects-absolute.raw \
    "$LOCKLINT" --check-locks declared-effects-absolute.c
compare_no_columns "absolute declared effects" \
    declared-effects-absolute.ref declared-effects-absolute.raw \
    declared-effects-absolute.out

run_capture "basic calls" calls-basic.out \
    "$LOCKLINT" --check-locks calls-basic.c
compare "basic calls" calls-basic.ref calls-basic.out

run_capture "indirect calls" indirect-calls.out \
    "$LOCKLINT" --check-locks indirect-calls.c
compare "indirect calls" indirect-calls.ref indirect-calls.out

#
# Verify calls, lock conditions, and effects across translation units.
#
run_capture "cross translation unit" cross.out "$LOCKLINT" --check-locks \
    cross-caller.c cross-callee.c
compare "cross translation unit" cross.ref cross.out

run_capture "cross translation unit visibility" visibility-cross.out \
    "$LOCKLINT" --check-locks visibility-cross-caller.c \
    visibility-cross-callee.c
compare "cross translation unit visibility" visibility-cross.ref \
    visibility-cross.out

#
# Verify the initial call-graph audit: direct call classification, function
# identity across translation units, and exact function-pointer escapes.
#
run_capture "calls callgraph" calls-callgraph.out \
    "$LOCKLINT" --dump-callgraph calls.c
compare "calls callgraph" calls-callgraph.ref calls-callgraph.out

run_capture "cross translation unit callgraph" cross-callgraph.out \
    "$LOCKLINT" --dump-callgraph cross-caller.c cross-callee.c
compare "cross translation unit callgraph" cross-callgraph.ref \
    cross-callgraph.out

run_capture "function pointers callgraph" function-pointers-callgraph.out \
    "$LOCKLINT" --dump-callgraph function-pointers.c
compare "function pointers callgraph" function-pointers-callgraph.ref \
    function-pointers-callgraph.out

run_capture "indirect calls callgraph" indirect-calls-callgraph.out \
    "$LOCKLINT" --dump-callgraph indirect-calls.c
compare "indirect calls callgraph" indirect-calls-callgraph.ref \
    indirect-calls-callgraph.out

run_capture "ambiguous call callgraph" ambiguous-call-callgraph.out \
    "$LOCKLINT" --dump-callgraph ambiguous-call-caller.c \
    ambiguous-call-first.c ambiguous-call-second.c
compare "ambiguous call callgraph" ambiguous-call-callgraph.ref \
    ambiguous-call-callgraph.out

#
# Verify external object and member identity across translation units.
#
run_capture "external objects" external-objects.raw \
    "$LOCKLINT" --check-locks external-objects-caller.c \
    external-objects-callee.c
compare_no_columns "external objects" external-objects.ref \
    external-objects.raw external-objects.out

#
# Verify exact local aliases, array-element identity, and constant container
# recovery.  Formal-argument alias relationships remain context-sensitive.
#
run_capture "object aliases" identity-aliases.out \
    "$LOCKLINT" --check-locks identity-aliases.c
compare "object aliases" identity-aliases.ref identity-aliases.out

run_capture "same-object formal aliases" identity-formals-same.out \
    "$LOCKLINT" --check-locks identity-formals.c
require_empty "same-object formal aliases" identity-formals-same.out

run_capture "different-object formal aliases" identity-formals-different.out \
    "$LOCKLINT" -DFORMAL_ALIAS_DIFFERENT --check-locks identity-formals.c
compare "different-object formal aliases" identity-formals-different.ref \
    identity-formals-different.out

#
# Verify annotations from a command-line forced include retain provenance.
#
run_capture "forced include" forced-include.raw \
    "$LOCKLINT" --check-locks -include forced-include.h forced-include.c
compare_no_columns "forced include" forced-include.ref forced-include.raw \
    forced-include.out

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
require_match "annotation errors" \
    "annotation lock 'error_state' names a structure" \
    annotation-errors.out
require_match "annotation errors" \
    "expected quoted protection scheme" annotation-errors.out
require_match "annotation errors" \
    "unresolved annotation name 'error_state::missing_scheme'" \
    annotation-errors.out
require_match "annotation errors" \
    "unresolved annotation name 'error_state::missing_readable'" \
    annotation-errors.out
require_match "annotation errors" \
    "unresolved annotation name 'error_state::missing_read_only'" \
    annotation-errors.out

#
# Verify complete mutex annotation names and recursive expansion.
#
run_capture "annotation names" annotation-names.raw \
    "$LOCKLINT" --check-locks annotation-names.c
compare_no_columns "annotation names" annotation-names.ref \
    annotation-names.raw annotation-names.out

#
# Verify type-scoped annotations through anonymous aggregate embedding.
#
run_capture "anonymous embedding" anonymous-embedding.raw \
    "$LOCKLINT" --check-locks anonymous-embedding.c
compare_no_columns "anonymous embedding" anonymous-embedding.ref \
    anonymous-embedding.raw anonymous-embedding.out

#
# Verify later protection declarations report override provenance.
#
run_capture "annotation name dump" annotation-names-dump.out \
    "$LOCKLINT" --dump-annotations annotation-names.c
require_match "annotation name dump" \
    'replaced by annotation-names.c:71:1' annotation-names-dump.out
require_match "annotation name dump" \
    'replaces annotation-names.c:69:1' annotation-names-dump.out

#
# Final report
#
if [ "$failures" -ne 0 ]; then
	echo "$failures locklint test(s) failed" >&2
	exit 1
fi

echo "All locklint tests passed"

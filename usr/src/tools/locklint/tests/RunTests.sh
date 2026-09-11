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

	sed \
	    -e 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: warning/\1: warning/' \
	    -e 's/\(.*:[0-9][0-9]*\):[0-9][0-9]*: locklint:/\1: locklint:/' \
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
require_match "declared upgrade effect expression" \
    "LOCK_UPGRADED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-errors.out
require_match "declared downgrade effect expression" \
    "LOCK_DOWNGRADED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-errors.out

run_failure "declared effect check errors" declared-effect-check-errors.out \
    "$LOCKLINT" --check-locks declared-effect-errors.c
require_match "checked declared mutex effect expression" \
    "MUTEX_ACQUIRED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out
require_match "checked declared release effect expression" \
    "LOCK_RELEASED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out
require_match "checked declared upgrade effect expression" \
    "LOCK_UPGRADED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out
require_match "checked declared downgrade effect expression" \
    "LOCK_DOWNGRADED_AS_SIDE_EFFECT requires a lock expression" \
    declared-effect-check-errors.out

#
# Verify LOCK_ORDER name resolution and optional comma separators.
#
run_capture "lock order annotations" lock-order-annotations.out \
    "$LOCKLINT" --dump-annotations lock-order-annotations.c
compare "lock order annotations" lock-order-annotations.ref \
    lock-order-annotations.out

run_capture "rwlock coverage annotations" rwlock-covers-locks-annotations.out \
    "$LOCKLINT" --dump-annotations rwlock-covers-locks.c
compare "rwlock coverage annotations" rwlock-covers-locks-annotations.ref \
    rwlock-covers-locks-annotations.out

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
require_match "stable primary diagnostic identifier" \
    '\[declared-order\]$' lock-order.out
reject_match "supporting diagnostic identifier" \
    'declared order requires.*\[[a-z-][a-z-]*\]$' lock-order.out

run_capture "condition wait state and order" condition-wait.raw \
    "$LOCKLINT" --check-locks condition-wait.c
compare_no_columns "condition wait state and order" condition-wait.ref \
    condition-wait.raw condition-wait.out

run_capture "condition wait events" condition-wait-events.out \
    "$LOCKLINT" --dump-events condition-wait.c
require_match "condition wait first mutex event" "WAIT state.first" \
    condition-wait-events.out
require_match "condition wait second mutex event" "WAIT state.second" \
    condition-wait-events.out
reject_match "condition wait condition-variable event" "WAIT state.cv" \
    condition-wait-events.out
reject_match "condition wait generic call event" "CALL cv_" \
    condition-wait-events.out

run_capture "mutex tryenter state" mutex-tryenter.raw \
    "$LOCKLINT" --check-locks mutex-tryenter.c
compare_no_columns "mutex tryenter state" mutex-tryenter.ref \
    mutex-tryenter.raw mutex-tryenter.out

run_capture "mutex tryenter events" mutex-tryenter-events.out \
    "$LOCKLINT" --dump-events mutex-tryenter.c
require_match "mutex tryenter event" "TRY-ACQUIRE state.first" \
    mutex-tryenter-events.out
reject_match "mutex tryenter generic call" "CALL mutex_tryenter" \
    mutex-tryenter-events.out

run_capture "mutex lock result state" mutex-lock-result.raw \
    "$LOCKLINT" --check-locks mutex-lock-result.c
compare_no_columns "mutex lock result state" mutex-lock-result.ref \
    mutex-lock-result.raw mutex-lock-result.out

run_capture "mutex lock result events" mutex-lock-result-events.out \
    "$LOCKLINT" --dump-events mutex-lock-result.c
require_match "mutex lock result acquire event" "ACQUIRE state.first" \
    mutex-lock-result-events.out
reject_match "mutex lock result generic call" "CALL mutex_lock" \
    mutex-lock-result-events.out

run_capture "mutex trylock state" mutex-trylock.raw \
    "$LOCKLINT" --check-locks mutex-trylock.c
compare_no_columns "mutex trylock state" mutex-trylock.ref \
    mutex-trylock.raw mutex-trylock.out

run_capture "mutex trylock events" mutex-trylock-events.out \
    "$LOCKLINT" --dump-events mutex-trylock.c
require_match "mutex trylock event" "TRY-ACQUIRE state.first" \
    mutex-trylock-events.out
reject_match "mutex trylock generic call" "CALL mutex_trylock" \
    mutex-trylock-events.out

run_capture "same-actual acquisition prefixes" \
    acquisition-prefix-alias-same.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=1 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "same-actual acquisition prefixes" \
    acquisition-prefix-alias-same.ref \
    acquisition-prefix-alias-same.raw acquisition-prefix-alias-same.out

run_capture "distinct-actual acquisition prefixes" \
    acquisition-prefix-alias-distinct.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=2 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "distinct-actual acquisition prefixes" \
    acquisition-prefix-alias-distinct.ref \
    acquisition-prefix-alias-distinct.raw \
    acquisition-prefix-alias-distinct.out

run_capture "wrapped same-actual acquisition prefixes" \
    acquisition-prefix-alias-wrapper-same.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=3 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "wrapped same-actual acquisition prefixes" \
    acquisition-prefix-alias-wrapper-same.ref \
    acquisition-prefix-alias-wrapper-same.raw \
    acquisition-prefix-alias-wrapper-same.out

run_capture "wrapped distinct-actual acquisition prefixes" \
    acquisition-prefix-alias-wrapper-distinct.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=4 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "wrapped distinct-actual acquisition prefixes" \
    acquisition-prefix-alias-wrapper-distinct.ref \
    acquisition-prefix-alias-wrapper-distinct.raw \
    acquisition-prefix-alias-wrapper-distinct.out

run_capture "internally aliased acquisition prefixes" \
    acquisition-prefix-alias-wrapper-internal.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=5 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "internally aliased acquisition prefixes" \
    acquisition-prefix-alias-wrapper-internal.ref \
    acquisition-prefix-alias-wrapper-internal.raw \
    acquisition-prefix-alias-wrapper-internal.out

run_capture "multiple changed acquisition prefixes" \
    acquisition-prefix-alias-multiple.raw "$LOCKLINT" \
    -DACQUISITION_PREFIX_ALIAS_VARIANT=6 --check-locks \
    acquisition-prefix-alias.c
compare_no_columns "multiple changed acquisition prefixes" \
    acquisition-prefix-alias-multiple.ref \
    acquisition-prefix-alias-multiple.raw \
    acquisition-prefix-alias-multiple.out

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

run_capture "rwlock downgrade state" rwlock-downgrade.raw \
    "$LOCKLINT" --check-locks rwlock-downgrade.c
compare_no_columns "rwlock downgrade state" rwlock-downgrade.ref \
    rwlock-downgrade.raw rwlock-downgrade.out

run_capture "rwlock downgrade events" rwlock-downgrade-events.out \
    "$LOCKLINT" --dump-events rwlock-downgrade.c
require_match "rwlock downgrade event" "DOWNGRADE state.lock" \
    rwlock-downgrade-events.out
reject_match "rwlock downgrade generic call" "CALL rw_downgrade" \
    rwlock-downgrade-events.out

run_capture "rwlock tryenter state" rwlock-tryenter.raw \
    "$LOCKLINT" --check-locks rwlock-tryenter.c
compare_no_columns "rwlock tryenter state" rwlock-tryenter.ref \
    rwlock-tryenter.raw rwlock-tryenter.out

run_capture "rwlock tryenter events" rwlock-tryenter-events.out \
    "$LOCKLINT" --dump-events rwlock-tryenter.c
require_match "rwlock reader tryenter event" \
    "TRY-ACQUIRE-READ state.first" rwlock-tryenter-events.out
require_match "rwlock writer tryenter event" \
    "TRY-ACQUIRE-WRITE state.first" rwlock-tryenter-events.out
reject_match "rwlock tryenter generic call" "CALL rw_tryenter" \
    rwlock-tryenter-events.out

run_capture "rwlock tryupgrade state" rwlock-tryupgrade.raw \
    "$LOCKLINT" --check-locks rwlock-tryupgrade.c
compare_no_columns "rwlock tryupgrade state" rwlock-tryupgrade.ref \
    rwlock-tryupgrade.raw rwlock-tryupgrade.out

run_capture "rwlock tryupgrade events" rwlock-tryupgrade-events.out \
    "$LOCKLINT" --dump-events rwlock-tryupgrade.c
require_match "rwlock tryupgrade event" "TRY-UPGRADE state.first" \
    rwlock-tryupgrade-events.out
reject_match "rwlock tryupgrade generic call" "CALL rw_tryupgrade" \
    rwlock-tryupgrade-events.out

run_capture "user rwlock calls" rwlock-calls-user.out \
    "$LOCKLINT" --check-locks rwlock-calls.c
compare "user rwlock calls" rwlock-calls.ref rwlock-calls-user.out

run_capture "kernel rwlock calls" rwlock-calls-kernel.out \
    "$LOCKLINT" -D_KERNEL --check-locks rwlock-calls.c
compare "kernel rwlock calls" rwlock-calls.ref rwlock-calls-kernel.out

run_capture "rwlock coverage" rwlock-covers-locks.raw \
    "$LOCKLINT" --check-locks rwlock-covers-locks.c
compare_no_columns "rwlock coverage" rwlock-covers-locks.ref \
    rwlock-covers-locks.raw rwlock-covers-locks.out

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

run_capture "not reached markers" not-reached-linearized.out \
    "$LOCKLINT" --dump-annotations --dump-linearized not-reached.c
grep -E 'context     0, tag 15|unreach' not-reached-linearized.out \
    > not-reached-markers.out
compare "not reached markers" not-reached-markers.ref \
    not-reached-markers.out

run_capture "not reached state" not-reached.raw \
    "$LOCKLINT" --check-locks not-reached.c
compare_no_columns "not reached state" not-reached.ref \
    not-reached.raw not-reached.out

run_capture "noreturn calls" noreturn.out \
    "$LOCKLINT" --check-locks noreturn.c
require_empty "noreturn calls" noreturn.out

run_capture "local exposure state" visibility-state.out \
    "$LOCKLINT" --check-locks visibility.c
compare "local exposure state" visibility-state.ref visibility-state.out

run_capture "competition depth" competition-depth.raw \
    "$LOCKLINT" --check-locks competition-depth.c
compare_no_columns "competition depth" competition-depth.ref \
    competition-depth.raw competition-depth.out

run_capture "competition calls" competition-calls.raw \
    "$LOCKLINT" --check-locks competition-calls.c
compare_no_columns "competition calls" competition-calls.ref \
    competition-calls.raw competition-calls.out

run_capture "competition contracts" competition-contracts.raw \
    "$LOCKLINT" --check-locks competition-contracts.c
compare_no_columns "competition contracts" competition-contracts.ref \
    competition-contracts.raw competition-contracts.out

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

run_capture "declared upgrade effects" rwlock-transition-effects-1.raw \
    "$LOCKLINT" -DRWLOCK_TRANSITION_EFFECT_VARIANT=1 --check-locks \
    rwlock-transition-effects.c
compare_no_columns "declared upgrade effects" \
    rwlock-transition-effects-1.ref rwlock-transition-effects-1.raw \
    rwlock-transition-effects-1.out

run_capture "declared downgrade effects" rwlock-transition-effects-2.raw \
    "$LOCKLINT" -DRWLOCK_TRANSITION_EFFECT_VARIANT=2 --check-locks \
    rwlock-transition-effects.c
compare_no_columns "declared downgrade effects" \
    rwlock-transition-effects-2.ref rwlock-transition-effects-2.raw \
    rwlock-transition-effects-2.out

run_capture "same-actual lock composition" alias-composition-same.out \
    "$LOCKLINT" -DALIAS_COMPOSITION_VARIANT=1 --check-locks \
    alias-composition.c
require_empty "same-actual lock composition" alias-composition-same.out

run_capture "distinct-actual lock composition" alias-composition-distinct.out \
    "$LOCKLINT" -DALIAS_COMPOSITION_VARIANT=2 --check-locks \
    alias-composition.c
require_empty "distinct-actual lock composition" \
    alias-composition-distinct.out

run_capture "same-actual wrapped lock composition" \
    alias-composition-wrapped-same.out "$LOCKLINT" \
    -DALIAS_COMPOSITION_VARIANT=3 --check-locks alias-composition.c
require_empty "same-actual wrapped lock composition" \
    alias-composition-wrapped-same.out

run_capture "distinct-actual wrapped lock composition" \
    alias-composition-wrapped-distinct.out "$LOCKLINT" \
    -DALIAS_COMPOSITION_VARIANT=4 --check-locks alias-composition.c
require_empty "distinct-actual wrapped lock composition" \
    alias-composition-wrapped-distinct.out

run_capture "internally aliased lock composition" \
    alias-composition-internal.out "$LOCKLINT" \
    -DALIAS_COMPOSITION_VARIANT=5 --check-locks alias-composition.c
require_empty "internally aliased lock composition" \
    alias-composition-internal.out

run_capture "recursive aliased lock composition" \
    alias-composition-recursive.out "$LOCKLINT" \
    -DALIAS_COMPOSITION_VARIANT=6 --check-locks alias-composition.c
require_empty "recursive aliased lock composition" \
    alias-composition-recursive.out

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

run_capture "assertion requirements" assertion-requirements.raw \
    "$LOCKLINT" --check-locks assertion-requirements.c
compare_no_columns "assertion requirements" assertion-requirements.ref \
    assertion-requirements.raw assertion-requirements.out

run_capture "same-actual assertion requirements" assertion-alias-same.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=1 --check-locks \
    assertion-alias.c
require_empty "same-actual assertion requirements" \
    assertion-alias-same.out

run_capture "distinct-actual assertion requirements" \
    assertion-alias-distinct.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=2 --check-locks assertion-alias.c
compare_no_columns "distinct-actual assertion requirements" \
    assertion-alias-distinct.ref assertion-alias-distinct.raw \
    assertion-alias-distinct.out

run_capture "opposite same-actual assertion requirements" \
    assertion-alias-opposite.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=3 --check-locks assertion-alias.c
compare_no_columns "opposite same-actual assertion requirements" \
    assertion-alias-opposite.ref assertion-alias-opposite.raw \
    assertion-alias-opposite.out

run_capture "wrapped same-actual assertion requirements" \
    assertion-alias-wrapper-same.out "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=4 --check-locks assertion-alias.c
require_empty "wrapped same-actual assertion requirements" \
    assertion-alias-wrapper-same.out

run_capture "wrapped distinct-actual assertion requirements" \
    assertion-alias-wrapper-distinct.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=5 --check-locks assertion-alias.c
compare_no_columns "wrapped distinct-actual assertion requirements" \
    assertion-alias-wrapper-distinct.ref \
    assertion-alias-wrapper-distinct.raw \
    assertion-alias-wrapper-distinct.out

run_capture "internally aliased assertion requirements" \
    assertion-alias-wrapper-internal.out "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=6 --check-locks assertion-alias.c
require_empty "internally aliased assertion requirements" \
    assertion-alias-wrapper-internal.out

run_capture "alias-invalidated assertion requirements" \
    assertion-alias-invalidated.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=7 --check-locks assertion-alias.c
compare_no_columns "alias-invalidated assertion requirements" \
    assertion-alias-invalidated.ref assertion-alias-invalidated.raw \
    assertion-alias-invalidated.out

run_capture "multiple assertion alias alternatives" \
    assertion-alias-multiple.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=8 --check-locks assertion-alias.c
compare_no_columns "multiple assertion alias alternatives" \
    assertion-alias-multiple.ref assertion-alias-multiple.raw \
    assertion-alias-multiple.out

run_capture "merged assertion alias alternatives" \
    assertion-alias-merged.out "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=9 --check-locks assertion-alias.c
require_empty "merged assertion alias alternatives" \
    assertion-alias-merged.out

run_capture "bounded assertion alias alternatives" \
    assertion-alias-overflow.raw "$LOCKLINT" \
    -DASSERTION_ALIAS_VARIANT=10 --check-locks assertion-alias.c
compare_no_columns "bounded assertion alias alternatives" \
    assertion-alias-overflow.ref assertion-alias-overflow.raw \
    assertion-alias-overflow.out

run_capture "assertion requirement wrappers" \
    assertion-requirement-wrappers.raw "$LOCKLINT" --check-locks \
    assertion-requirement-wrappers.c
compare_no_columns "assertion requirement wrappers" \
    assertion-requirement-wrappers.ref assertion-requirement-wrappers.raw \
    assertion-requirement-wrappers.out

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

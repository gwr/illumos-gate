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
		return 1
	fi
	return 0
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

run_capture "phase timing" times.out "$LOCKLINT" --times --dump-parsed smoke.c
for phase in initialize input-parse input-identities input-types \
    input-evidence \
    input-symbols input-cleanup commands analysis-setup fixed-point \
    diag-decl-effects \
    diag-transitions diag-decl-order diag-assertions diag-comp-underflow \
    diag-comp-effects diag-comp-assert diag-protected diag-assumed \
    diag-returns diag-observed-order measurement analysis-cleanup \
    final-output total
do
	require_match "phase timing $phase" \
	    "^time $phase *[0-9][0-9]*\\.[0-9]\\{3\\}$" times.out
done
if [ "$(grep -c '^time ' times.out)" -ne 25 ]; then
	fail "phase timing: expected exactly twenty-five timing lines"
fi

run_capture "signed one-bit field" signed-one-bit-field.out \
    "$LOCKLINT" --dump-parsed signed-one-bit-field.c
require_match "signed one-bit field" signed_one_bit_value \
    signed-one-bit-field.out

run_capture "linearized smoke" linearized.out \
    "$LOCKLINT" --dump-linearized smoke.c
if ! grep -Eq 'load|store' linearized.out; then
	fail "linearized smoke: missing load or store"
fi

#
# Verify native and historical preprocessor compatibility modes.
#
run_capture "native preprocessor mode" preprocessor-compat-native.out \
    "$LOCKLINT" --dump-parsed preprocessor-compat.c
require_match "native preprocessor mode" native_mode \
    preprocessor-compat-native.out
reject_match "native preprocessor mode" osll_compatibility_mode \
    preprocessor-compat-native.out

run_capture "OSLL preprocessor compatibility mode" \
    preprocessor-compat-osll.out "$LOCKLINT" --compat=osll \
    --dump-parsed preprocessor-compat.c
require_match "OSLL preprocessor compatibility mode" \
    osll_compatibility_mode preprocessor-compat-osll.out
reject_match "OSLL preprocessor compatibility mode" native_mode \
    preprocessor-compat-osll.out

run_failure "unknown preprocessor compatibility mode" \
    preprocessor-compat-unknown.out "$LOCKLINT" --compat=unknown \
    --dump-parsed preprocessor-compat.c
require_match "unknown preprocessor compatibility mode" \
    "unknown compatibility mode 'unknown'" preprocessor-compat-unknown.out

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
# Verify real lock events resolve repeated formal-member accesses to one
# canonical identity before lock-state transitions are enabled.
#
run_capture "event lock identities" event-lock-identities.out \
    "$LOCKLINT" --dump-contexts events.c
require_match "event lock identities" \
    '^lock-identities created 1 reused 4 unresolved 0 retained 1$' \
    event-lock-identities.out
require_match "event lock identity types" \
    '^lock-identity-types unspecified 0 object 0 symbol 0 pseudo 1$' \
    event-lock-identities.out
require_match "event lock identity objects" \
    '^lock-identity-analysis-objects 1$' event-lock-identities.out

run_capture "global lock identities" lock-identity-globals.out \
    "$LOCKLINT" --dump-contexts lock-identity-globals.c
require_match "global lock identities" \
    '^lock-identities created 2 reused 2 unresolved 0 retained 2$' \
    lock-identity-globals.out
require_match "global lock identity types" \
    '^lock-identity-types unspecified 0 object 2 symbol 0 pseudo 0$' \
    lock-identity-globals.out
require_match "global lock identity objects" \
    '^lock-identity-analysis-objects 2$' lock-identity-globals.out
reject_match "global lock identities" 'warning:' \
    lock-identity-globals.out

run_capture "local lock identity" lock-identity-local.out \
    "$LOCKLINT" --dump-contexts lock-identity-local.c
require_match "local lock identity" \
    '^lock-identities created 3 reused 8 unresolved 0 retained 3$' \
    lock-identity-local.out
require_match "local lock identity type" \
    '^lock-identity-types unspecified 0 object 0 symbol 3 pseudo 0$' \
    lock-identity-local.out
require_match "local lock identity objects" \
    '^lock-identity-analysis-objects 3$' lock-identity-local.out
require_match "local lock transitions" \
    '^lock-transitions applied 9 deferred 0$' lock-identity-local.out
require_match "local lock returns" \
    '^return-states mapped 8 locks-filtered 2$' lock-identity-local.out
require_match "local lock contexts" \
    '^contexts created 7 reused 1$' lock-identity-local.out
require_match "local lock states" \
    '^distribution semantic-states/function samples 6 total 12 max 2$' \
    lock-identity-local.out
require_match "local held-lock states" \
    '^distribution locks/semantic-state samples 12 total 6 max 1$' \
    lock-identity-local.out
require_match "local helper contexts" \
    '^maximum contexts/function 2 function local_helper tu=lock-identity-local.c$' \
    lock-identity-local.out
require_match "local unmatched release" \
    "lock-identity-local.c:79:19: warning: locklint: lock 'local_lock' is not held \\[lock-not-held\\]" \
    lock-identity-local.out
require_match "local duplicate acquire" \
    "lock-identity-local.c:81:20: warning: locklint: lock 'local_lock' is already held \\[lock-already-held\\]" \
    lock-identity-local.out
require_match "formal held on return" \
    "lock-identity-local.c:39:21: warning: locklint: lock 'lock' held on return from 'acquire_helper' \\[lock-held-on-return\\]" \
    lock-identity-local.out
require_match "local held on return" \
    "lock-identity-local.c:53:22: warning: locklint: lock 'local_lock' held on return from 'local_lock_helper' \\[lock-held-on-return\\]" \
    lock-identity-local.out
require_match "local maybe held on return" \
    "lock-identity-local.c:62:30: warning: locklint: lock 'local_lock' held on only some paths returning from 'local_maybe_lock_helper' \\[lock-maybe-held-on-return\\]" \
    lock-identity-local.out
require_match "local ignored tryenter maybe held on return" \
    "lock-identity-local.c:83:32: warning: locklint: lock 'local_lock' held on only some paths returning from 'lock_identity_local' \\[lock-maybe-held-on-return\\]" \
    lock-identity-local.out
if [ "$(grep -c 'warning:' lock-identity-local.out)" -ne 6 ]; then
	fail "local lock diagnostics: expected exactly six warnings"
fi

run_capture "member lock identities" lock-identity-members.out \
    "$LOCKLINT" --dump-contexts lock-identity-members.c
require_match "member lock identities" \
    '^binding-identities composed 4$' lock-identity-members.out
require_match "member lock identities" \
    '^lock-identities created 2 reused 3 unresolved 0 retained 2$' \
    lock-identity-members.out
require_match "member lock identity types" \
    '^lock-identity-types unspecified 0 object 0 symbol 0 pseudo 2$' \
    lock-identity-members.out
require_match "member lock identity objects" \
    '^lock-identity-analysis-objects 1$' lock-identity-members.out
reject_match "member lock identities" 'warning:' lock-identity-members.out

run_capture "derived lock identities" lock-identity-derived.out \
    "$LOCKLINT" --dump-contexts lock-identity-derived.c
require_match "derived lock identities" \
    '^lock-identities created 2 reused 2 unresolved 0 retained 2$' \
    lock-identity-derived.out
require_match "derived lock identity types" \
    '^lock-identity-types unspecified 0 object 0 symbol 0 pseudo 2$' \
    lock-identity-derived.out
require_match "derived lock identity objects" \
    '^lock-identity-analysis-objects 1$' lock-identity-derived.out
reject_match "derived lock identities" 'warning:' lock-identity-derived.out

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

run_capture "declared lock order cycle" lock-order-cycle.out \
    "$LOCKLINT" --check-locks lock-order-cycle.c
compare "declared lock order cycle" lock-order-cycle.ref \
    lock-order-cycle.out

run_capture "declared lock order" lock-order.out \
    "$LOCKLINT" --check-locks lock-order.c
compare "declared lock order" lock-order.ref lock-order.out

run_capture "observed lock order" lock-order-observed.out \
    "$LOCKLINT" --check-locks lock-order-observed.c
compare "observed lock order" lock-order-observed.ref \
    lock-order-observed.out

run_capture "conditional local declared lock order" \
    lock-order-local-conditional.out \
    "$LOCKLINT" --check-locks lock-order-local-conditional.c
compare "conditional local declared lock order" \
    lock-order-local-conditional.ref lock-order-local-conditional.out

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

run_capture "mutex tryenter events" mutex-tryenter-events.out \
    "$LOCKLINT" --dump-events mutex-tryenter.c
require_match "mutex tryenter event" "TRY-ACQUIRE state.first" \
    mutex-tryenter-events.out
reject_match "mutex tryenter generic call" "CALL mutex_tryenter" \
    mutex-tryenter-events.out

run_capture "mutex lock result events" mutex-lock-result-events.out \
    "$LOCKLINT" --dump-events mutex-lock-result.c
require_match "mutex lock result acquire event" "ACQUIRE state.first" \
    mutex-lock-result-events.out
reject_match "mutex lock result generic call" "CALL mutex_lock" \
    mutex-lock-result-events.out

run_capture "mutex trylock events" mutex-trylock-events.out \
    "$LOCKLINT" --dump-events mutex-trylock.c
require_match "mutex trylock event" "TRY-ACQUIRE state.first" \
    mutex-trylock-events.out
reject_match "mutex trylock generic call" "CALL mutex_trylock" \
    mutex-trylock-events.out

#
# Verify independent protection mechanism, unlocked-read, and read-only
# policy dimensions.
#
run_capture "data policy annotations" data-policy-annotations.out \
    "$LOCKLINT" --dump-annotations data-policy.c
compare "data policy annotations" data-policy-annotations.ref \
    data-policy-annotations.out

#
# Verify command-file readable policy after all translation units have been
# parsed, including a type declared separately in each translation unit.
#
run_capture "command readable provenance" command-readable-annotations.out \
    "$LOCKLINT" --dump-annotations --cf commands/readable.cf \
    commands/readable.c commands/readable-other.c
require_match "command readable type provenance" \
    "commands/readable.cf:2: DATA_READABLE_WITHOUT_LOCK command_state::readable" \
    command-readable-annotations.out
require_match "command readable object provenance" \
    "commands/readable.cf:4: DATA_READABLE_WITHOUT_LOCK command_global" \
    command-readable-annotations.out

run_capture "command type dump" command-types.out \
    "$LOCKLINT" --dump-types --dump-statistics \
    commands/readable.c commands/readable-other.c
if [ "$(grep -c '^type command_state kind=struct ' command-types.out)" \
    -ne 2 ]; then
	fail "command type dump: expected two command_state instances"
fi
if [ "$(grep -c '^type command_state_t kind=struct ' command-types.out)" \
    -ne 2 ]; then
	fail "command type dump: expected two command_state_t instances"
fi
if [ "$(grep -c '^type duplicate_command_type kind=struct ' \
    command-types.out)" -ne 2 ]; then
	fail "command type dump: expected two ambiguous type instances"
fi
require_match "command type dump summary" '^types [1-9][0-9]*$' \
    command-types.out
require_match "command type registry size" \
    '^statistics type_registry_insertions 12$' command-types.out
for statistic in type_registration_symbols_visited \
    type_registration_nodes_visited type_registry_find \
    type_registry_duplicates type_registry_comparisons
do
	require_match "command type statistic $statistic" \
	    "^statistics $statistic [1-9][0-9]*$" command-types.out
done
reject_match "command type dump enum" 'COMMAND_READABLE_ENUM' \
    command-types.out
reject_match "command type dump object" '^type command_object ' \
    command-types.out

run_failure "command readable arity" command-readable-arity.out \
    "$LOCKLINT" --cf commands/readable-arity.cf \
    commands/readable.c
require_match "command readable arity" \
    "declare readable requires one data name" command-readable-arity.out

run_failure "command readable unresolved name" \
    command-readable-unresolved.out "$LOCKLINT" \
    --cf commands/readable-unresolved.cf commands/readable.c
require_match "command readable unresolved name" \
    "unresolved data name 'missing_command_object'" \
    command-readable-unresolved.out

run_failure "command object is not a type" \
    command-readable-object-as-type.out "$LOCKLINT" \
    --cf commands/readable-object-as-type.cf \
    commands/readable.c
require_match "command object is not a type" \
    "unresolved data name 'command_object::readable'" \
    command-readable-object-as-type.out

run_failure "command enum is not an object" command-readable-enum.out \
    "$LOCKLINT" --cf commands/readable-enum.cf \
    commands/readable.c
require_match "command enum is not an object" \
    "unresolved data name 'COMMAND_READABLE_ENUM'" \
    command-readable-enum.out

run_failure "command readable ambiguous type" \
    command-readable-ambiguous.out "$LOCKLINT" \
    --cf commands/readable-ambiguous.cf \
    commands/readable.c commands/readable-other.c
require_match "command readable ambiguous type" \
    "ambiguous data name 'duplicate_command_type::value'" \
    command-readable-ambiguous.out

run_capture "rwlock annotations" rwlock-annotations.out \
    "$LOCKLINT" --dump-annotations rwlock.c
compare "rwlock annotations" rwlock-annotations.ref \
    rwlock-annotations.out

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

run_capture "rwlock downgrade events" rwlock-downgrade-events.out \
    "$LOCKLINT" --dump-events rwlock-downgrade.c
require_match "rwlock downgrade event" "DOWNGRADE state.lock" \
    rwlock-downgrade-events.out
reject_match "rwlock downgrade generic call" "CALL rw_downgrade" \
    rwlock-downgrade-events.out

run_capture "rwlock tryenter events" rwlock-tryenter-events.out \
    "$LOCKLINT" --dump-events rwlock-tryenter.c
require_match "rwlock reader tryenter event" \
    "TRY-ACQUIRE-READ state.first" rwlock-tryenter-events.out
require_match "rwlock writer tryenter event" \
    "TRY-ACQUIRE-WRITE state.first" rwlock-tryenter-events.out
reject_match "rwlock tryenter generic call" "CALL rw_tryenter" \
    rwlock-tryenter-events.out

run_capture "rwlock tryupgrade events" rwlock-tryupgrade-events.out \
    "$LOCKLINT" --dump-events rwlock-tryupgrade.c
require_match "rwlock tryupgrade event" "TRY-UPGRADE state.first" \
    rwlock-tryupgrade-events.out
reject_match "rwlock tryupgrade generic call" "CALL rw_tryupgrade" \
    rwlock-tryupgrade-events.out

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

#
# Verify competition transitions and dominator-selected loop widening converge.
#
run_capture "competition context convergence" competition-depth-contexts.out \
    "$LOCKLINT" --dump-contexts competition-depth.c
require_match "competition transitions" \
    '^competition-transitions applied 34 backedges-widened 3 backedges-covered 3$' \
    competition-depth-contexts.out
require_match "competition semantic states" \
    '^semantic-states created 36 reused 8$' competition-depth-contexts.out
require_match "competition point states" \
    '^point-states created 144 reused 4$' competition-depth-contexts.out
require_match "visibility set baseline" \
    '^visibility-sets created 10 reused 0 retained 10$' \
    competition-depth-contexts.out
require_match "visibility sets per function baseline" \
    '^distribution visibility-sets/function samples 10 total 10 max 1$' \
    competition-depth-contexts.out
require_match "visibility entries per set baseline" \
    '^distribution visibility-entries/set samples 10 total 0 max 0$' \
    competition-depth-contexts.out

#
# Verify whole-object and member visibility updates, comma-separated targets,
# branch-specific states, explicit visible overrides, and invalid operands.
#
run_capture "visibility context transitions" visibility-contexts.out \
    "$LOCKLINT" --dump-contexts visibility.c
require_match "visibility transition counts" \
    '^visibility-transitions applied 25 deferred 2 unresolved 1$' \
    visibility-contexts.out
require_match "visibility canonical sets" \
    '^visibility-sets created 52 reused 13 retained 52$' \
    visibility-contexts.out
require_match "visibility sets per function" \
    '^distribution visibility-sets/function samples 26 total 52 max 5$' \
    visibility-contexts.out
require_match "visibility entries per set" \
    '^distribution visibility-entries/set samples 52 total 30 max 2$' \
    visibility-contexts.out
require_match "invalid visibility operand" \
    "visibility.c:151:9: warning: locklint: visibility annotation has no object \\[visibility-no-object\\]" \
    visibility-contexts.out
if [ "$(grep -c '\[visibility-no-object\]' visibility-contexts.out)" \
    -ne 1 ]; then
	fail "visibility context transitions: expected exactly one warning"
fi

#
# Verify local access decisions use the most-specific visibility region.
# Restrict this exact reference to the local visibility/read-only exercises;
# caller and cross-translation-unit visibility remain separate work.
#
run_capture "local visibility diagnostics" visibility-local-diagnostics.out \
    "$LOCKLINT" --check-locks visibility.c
grep -E \
    'visibility.c:(8[0-9]|9[0-9]|1[0-8][0-9]|190):.*\[(unprotected-access|conditional-protection|read-only-(maybe-)?visible)\]' \
    visibility-local-diagnostics.out > visibility-local.out
compare "local visibility diagnostics" visibility-local.ref \
    visibility-local.out

#
# Verify the complete visibility-state diagnostics against the original
# locklint reference.
#
compare "visibility state diagnostics" visibility-state.ref \
    visibility-local-diagnostics.out

#
# Verify direct and wrapped callees return exact visibility state, including
# conditional exits, globals, recursion, and nested formal-relative regions.
#
run_capture "call visibility diagnostics" visibility-calls.out \
    "$LOCKLINT" --check-locks visibility-calls.c
compare "call visibility diagnostics" visibility-calls.ref \
    visibility-calls.out

#
# Verify formal, global, and nested visibility effects retain canonical
# identities across translation units independent of input order.
#
run_capture "cross translation unit visibility diagnostics" \
    visibility-cross.out "$LOCKLINT" --check-locks \
    visibility-cross-caller.c visibility-cross-callee.c
compare "cross translation unit visibility diagnostics" \
    visibility-cross.ref visibility-cross.out

run_capture "reversed cross translation unit visibility diagnostics" \
    visibility-cross-reversed.out "$LOCKLINT" --check-locks \
    visibility-cross-callee.c visibility-cross-caller.c
compare "reversed cross translation unit visibility diagnostics" \
    visibility-cross.ref visibility-cross-reversed.out

#
# Verify declared competition side effects against every exact return state.
#
run_capture "declared competition effect diagnostics" \
    competition-contracts.out "$LOCKLINT" --check-locks \
    competition-contracts.c
compare "declared competition effect diagnostics" \
    competition-contracts.ref competition-contracts.out

#
# Verify declared mutex, reader, and writer acquisitions and generic releases
# against every exact exit from distinct synthetic contract contexts.
#
run_capture "declared lock effect diagnostics" declared-effects.out \
    "$LOCKLINT" --check-locks declared-effects.c
compare "declared lock effect diagnostics" declared-effects.ref \
    declared-effects.out

run_capture "declared release effect diagnostics" \
    declared-releases.out "$LOCKLINT" --check-locks declared-releases.c
compare "declared release effect diagnostics" \
    declared-releases.ref declared-releases.out

run_capture "declared upgrade effect diagnostics" \
    rwlock-transition-effects-1.out "$LOCKLINT" --check-locks \
    -DRWLOCK_TRANSITION_EFFECT_VARIANT=1 rwlock-transition-effects.c
compare "declared upgrade effect diagnostics" \
    rwlock-transition-effects-1.ref rwlock-transition-effects-1.out

run_capture "declared downgrade effect diagnostics" \
    rwlock-transition-effects-2.out "$LOCKLINT" --check-locks \
    -DRWLOCK_TRANSITION_EFFECT_VARIANT=2 rwlock-transition-effects.c
compare "declared downgrade effect diagnostics" \
    rwlock-transition-effects-2.ref rwlock-transition-effects-2.out

#
# Verify canonical absolute lock identities retain declared validation and
# exact acquired/released state through direct calls and formal wrappers.
#
run_capture "absolute declared effect diagnostics" \
    declared-effects-absolute.out "$LOCKLINT" --check-locks \
    declared-effects-absolute.c
compare "absolute declared effect diagnostics" \
    declared-effects-absolute.ref declared-effects-absolute.out

run_capture "lock effect diagnostics" effects.out \
    "$LOCKLINT" --check-locks effects.c
compare "lock effect diagnostics" effects.ref effects.out

run_capture "user rwlock core state" rwlock-core-user.out \
    "$LOCKLINT" --check-locks -DLOCKLINT_RWLOCK_CORE_ONLY rwlock.c
compare "user rwlock core state" rwlock-core.ref rwlock-core-user.out

run_capture "kernel rwlock core state" rwlock-core-kernel.out \
    "$LOCKLINT" --check-locks -D_KERNEL -DLOCKLINT_RWLOCK_CORE_ONLY rwlock.c
compare "kernel rwlock core state" rwlock-core.ref rwlock-core-kernel.out

run_capture "user rwlock assertions" rwlock-user.out \
    "$LOCKLINT" --check-locks rwlock.c
compare "user rwlock assertions" rwlock-user.ref rwlock-user.out

run_capture "kernel rwlock assertions" rwlock-kernel.out \
    "$LOCKLINT" --check-locks -D_KERNEL rwlock.c
compare "kernel rwlock assertions" rwlock-kernel.ref rwlock-kernel.out

run_capture "direct assertion call sites" assertion-requirements.out \
    "$LOCKLINT" --check-locks assertion-requirements.c
compare "direct assertion call sites" assertion-requirements.ref \
    assertion-requirements.out

run_capture "wrapped assertion call sites" \
    assertion-requirement-wrappers.out \
    "$LOCKLINT" --check-locks assertion-requirement-wrappers.c
compare "wrapped assertion call sites" \
    assertion-requirement-wrappers.ref \
    assertion-requirement-wrappers.out

run_capture "same-actual assertion aliases" assertion-alias-same.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=1 assertion-alias.c
reject_match "same-actual assertion aliases" "warning:" \
    assertion-alias-same.out

run_capture "distinct assertion aliases" assertion-alias-distinct.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=2 assertion-alias.c
compare "distinct assertion aliases" assertion-alias-distinct.ref \
    assertion-alias-distinct.out

run_capture "opposite assertion aliases" assertion-alias-opposite.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=3 assertion-alias.c
compare "opposite assertion aliases" assertion-alias-opposite.ref \
    assertion-alias-opposite.out

run_capture "same-actual wrapped assertion aliases" \
    assertion-alias-wrapper-same.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=4 assertion-alias.c
reject_match "same-actual wrapped assertion aliases" "warning:" \
    assertion-alias-wrapper-same.out

run_capture "distinct wrapped assertion aliases" \
    assertion-alias-wrapper-distinct.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=5 assertion-alias.c
compare "distinct wrapped assertion aliases" \
    assertion-alias-wrapper-distinct.ref \
    assertion-alias-wrapper-distinct.out

run_capture "internally aliased assertion" assertion-alias-internal.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=6 assertion-alias.c
reject_match "internally aliased assertion" "warning:" \
    assertion-alias-internal.out

run_capture "invalidated assertion alias" assertion-alias-invalidated.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=7 assertion-alias.c
compare "invalidated assertion alias" assertion-alias-invalidated.ref \
    assertion-alias-invalidated.out

run_capture "multiple assertion aliases" assertion-alias-multiple.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=8 assertion-alias.c
compare "multiple assertion aliases" assertion-alias-multiple.ref \
    assertion-alias-multiple.out

run_capture "merged assertion aliases" assertion-alias-merged.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=9 assertion-alias.c
reject_match "merged assertion aliases" "warning:" \
    assertion-alias-merged.out

run_capture "assertion alias overflow replacement" \
    assertion-alias-overflow.out \
    "$LOCKLINT" --check-locks -DASSERTION_ALIAS_VARIANT=10 assertion-alias.c
compare "assertion alias overflow replacement" \
    assertion-alias-overflow.ref assertion-alias-overflow.out

run_capture "user rwlock call state" rwlock-calls-user.out \
    "$LOCKLINT" --check-locks rwlock-calls.c
compare "user rwlock call state" rwlock-calls.ref rwlock-calls-user.out

run_capture "kernel rwlock call state" rwlock-calls-kernel.out \
    "$LOCKLINT" --check-locks -D_KERNEL rwlock-calls.c
compare "kernel rwlock call state" rwlock-calls.ref rwlock-calls-kernel.out

run_capture "rwlock downgrade state" rwlock-downgrade.out \
    "$LOCKLINT" --check-locks rwlock-downgrade.c
compare "rwlock downgrade state" rwlock-downgrade.ref \
    rwlock-downgrade.out

run_capture "rwlock tryupgrade state" rwlock-tryupgrade.out \
    "$LOCKLINT" --check-locks rwlock-tryupgrade.c
compare "rwlock tryupgrade state" rwlock-tryupgrade.ref \
    rwlock-tryupgrade.out

run_capture "rwlock tryenter state" rwlock-tryenter.out \
    "$LOCKLINT" --check-locks rwlock-tryenter.c
compare "rwlock tryenter state" rwlock-tryenter.ref rwlock-tryenter.out

run_capture "mutex tryenter state" mutex-tryenter.out \
    "$LOCKLINT" --check-locks mutex-tryenter.c
compare "mutex tryenter state" mutex-tryenter.ref mutex-tryenter.out

run_capture "mutex trylock state" mutex-trylock.out \
    "$LOCKLINT" --check-locks mutex-trylock.c
compare "mutex trylock state" mutex-trylock.ref mutex-trylock.out

run_capture "mutex lock result state" mutex-lock-result.out \
    "$LOCKLINT" --check-locks mutex-lock-result.c
compare "mutex lock result state" mutex-lock-result.ref \
    mutex-lock-result.out

run_capture "condition wait state and order" condition-wait.out \
    "$LOCKLINT" --check-locks condition-wait.c
compare "condition wait state and order" condition-wait.ref \
    condition-wait.out

run_capture "competition protected accesses" competition-accesses.out \
    "$LOCKLINT" -DCOMPETITION_ACCESS_ONLY --check-locks competition-depth.c
for location in 50 52 54 70 83 115
do
	require_match "definite competing access" \
	    "competition-depth.c:$location:27: warning: locklint: protected member 'protected' modified without holding 'lock' \\[unprotected-access\\]" \
	    competition-accesses.out
done
for location in 104 154
do
	require_match "conditional competing access" \
	    "competition-depth.c:$location:27: warning: locklint: protection for member 'protected' is not established on every path \\[conditional-protection\\]" \
	    competition-accesses.out
done
if [ "$(grep -Ec '\[(unprotected-access|conditional-protection)\]' \
    competition-accesses.out)" -ne 8 ]; then
	fail "competition protected accesses: expected exactly eight warnings"
fi

require_match "definite unmatched competition decrement" \
    "competition-depth.c:91:9: warning: locklint: competition depth decremented below zero \\[competition-underflow\\]" \
    competition-depth-contexts.out
require_match "definite recovered competition decrement" \
    "competition-depth.c:138:9: warning: locklint: competition depth decremented below zero \\[competition-underflow\\]" \
    competition-depth-contexts.out
require_match "possible loop competition decrement" \
    "competition-depth.c:151:17: warning: locklint: competition depth may be decremented below zero \\[competition-maybe-underflow\\]" \
    competition-depth-contexts.out
if [ "$(grep -Ec '\[competition-(maybe-)?underflow\]' \
    competition-depth-contexts.out)" -ne 3 ]; then
	fail "competition underflow diagnostics: expected exactly three warnings"
fi
for location in 125 127 129
do
	require_match "definite competing read-only write" \
	    "competition-depth.c:$location:27: warning: locklint: read-only data 'read_only' modified while visible to competing threads \\[read-only-visible\\]" \
	    competition-depth-contexts.out
done
require_match "possible competing read-only write" \
    "competition-depth.c:161:27: warning: locklint: read-only data 'read_only' may be modified while visible to competing threads \\[read-only-maybe-visible\\]" \
    competition-depth-contexts.out
if [ "$(grep -Ec '\[read-only-(maybe-)?visible\]' \
    competition-depth-contexts.out)" -ne 4 ]; then
	fail "read-only competition diagnostics: expected exactly four warnings"
fi
reject_match "first non-competing read-only write" \
    "competition-depth.c:123:27:.*\\[read-only-" \
    competition-depth-contexts.out
reject_match "restored non-competing read-only write" \
    "competition-depth.c:131:27:.*\\[read-only-" \
    competition-depth-contexts.out

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
# Verify that GNU extern-inline bodies belong to their including translation
# units and do not compete with each other or one emitted external definition.
#
run_capture "GNU extern inline implementations" \
    gnu-extern-inline-callgraph.out \
    "$LOCKLINT" --dump-callgraph gnu-extern-inline-first.c \
    gnu-extern-inline-second.c gnu-extern-inline-external.c
compare "GNU extern inline implementations" \
    gnu-extern-inline-callgraph.ref gnu-extern-inline-callgraph.out

run_capture "GNU extern inline reversed input order" \
    gnu-extern-inline-reversed.out \
    "$LOCKLINT" --dump-callgraph gnu-extern-inline-external.c \
    gnu-extern-inline-first.c gnu-extern-inline-second.c
reject_match "GNU extern inline reversed first implementation" \
    "call gnu-extern-inline-first.c:22:41" \
    gnu-extern-inline-reversed.out
reject_match "GNU extern inline reversed second implementation" \
    "call gnu-extern-inline-second.c:22:41" \
    gnu-extern-inline-reversed.out
require_match "GNU extern inline reversed first escape" \
    "escape gnu-extern-inline-first.c:28:5.*tu=gnu-extern-inline-external.c" \
    gnu-extern-inline-reversed.out
require_match "GNU extern inline reversed second escape" \
    "escape gnu-extern-inline-second.c:28:5.*tu=gnu-extern-inline-external.c" \
    gnu-extern-inline-reversed.out

run_capture "GNU extern inline bodies" gnu-extern-inline-linearized.out \
    "$LOCKLINT" --dump-linearized gnu-extern-inline-first.c \
    gnu-extern-inline-second.c gnu-extern-inline-external.c
require_match "GNU extern inline first body" 'ret.32      $1' \
    gnu-extern-inline-linearized.out
require_match "GNU extern inline second body" 'ret.32      $2' \
    gnu-extern-inline-linearized.out
require_match "GNU extern inline external body" 'ret.32      $3' \
    gnu-extern-inline-linearized.out

run_capture "GNU extern inline direct only" \
    gnu-extern-inline-direct-only.out \
    "$LOCKLINT" --dump-linearized gnu-extern-inline-direct-only.c
require_match "GNU extern inline direct-only implementation" \
    'ret.32      $5' gnu-extern-inline-direct-only.out

run_capture "GNU extern inline body first" \
    gnu-extern-inline-body-first.out \
    "$LOCKLINT" --dump-linearized gnu-extern-inline-body-first.c
require_match "GNU extern inline body-first implementation" \
    'ret.32      $6' gnu-extern-inline-body-first.out

run_capture "forced GNU extern inline" gnu-extern-inline-forced.out \
    "$LOCKLINT" --dump-linearized -include gnu-extern-inline-forced.h \
    gnu-extern-inline-forced.c
require_match "forced GNU extern inline implementation" \
    'ret.32      $7' gnu-extern-inline-forced.out

run_capture "GNU extern inline without external definition" \
    gnu-extern-inline-no-external.out \
    "$LOCKLINT" --dump-callgraph gnu-extern-inline-no-external.c
compare "GNU extern inline without external definition" \
    gnu-extern-inline-no-external.ref \
    gnu-extern-inline-no-external.out

#
# Verify that forced includes with internal declarations remain rejected for
# multiple callgraph inputs.
#
run_failure "forced include multiple inputs" forced-include-multiple.out \
    "$LOCKLINT" --dump-callgraph -include forced-include.h forced-include.c \
    forced-include-second.c
require_match "forced include multiple inputs" \
    "multiple inputs with initialization-time internal declarations" \
    forced-include-multiple.out

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
# Verify later protection declarations report override provenance.
#
run_capture "annotation name dump" annotation-names-dump.out \
    "$LOCKLINT" --dump-annotations annotation-names.c
require_match "annotation name dump" \
    'replaced by annotation-names.c:72:1' annotation-names-dump.out
require_match "annotation name dump" \
    'replaces annotation-names.c:70:1' annotation-names-dump.out


#
# Verify mutex-protected accesses and acquire/release diagnostics.
#
run_capture "lock transition diagnostics" lock-transition-diagnostics.out \
    "$LOCKLINT" --check-locks check.c
require_match "unprotected read diagnostic" \
    "check.c:49:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "unprotected write diagnostic" \
    "check.c:103:14: warning: locklint: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "second unprotected read diagnostic" \
    "check.c:55:30: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "conditional protection diagnostic" \
    "check.c:65:22: warning: locklint: protection for member 'value' is not established on every path \\[conditional-protection\\]" \
    lock-transition-diagnostics.out
require_match "mixed release diagnostic" \
    "check.c:66:19: warning: locklint: lock 'lock' may not be held \\[lock-maybe-not-held\\]" \
    lock-transition-diagnostics.out
require_match "mixed acquire diagnostic" \
    "check.c:96:20: warning: locklint: lock 'lock' may already be held \\[lock-maybe-already-held\\]" \
    lock-transition-diagnostics.out
require_match "conditional held on return" \
    "check.c:88:30: warning: locklint: lock 'lock' held on only some paths returning from 'check_side_effect' \\[lock-maybe-held-on-return\\]" \
    lock-transition-diagnostics.out
if [ "$(grep -c 'warning:' lock-transition-diagnostics.out)" -ne 7 ]; then
	fail "lock transition diagnostics: expected exactly seven warnings"
fi

#
# Verify structure-valued global and member mutex identities.
#
run_capture "structure-valued mutex diagnostics" struct-lock-diagnostics.out \
    "$LOCKLINT" --check-locks struct-lock.c
compare "structure-valued mutex diagnostics" struct-lock.ref \
    struct-lock-diagnostics.out

#
# Verify that competition assertions validate but do not alter the state
# reaching subsequent protected accesses.
#
run_capture "competition assertion diagnostics" assertion-diagnostics.out \
    "$LOCKLINT" --check-locks assertions.c
compare "assertion diagnostics" assertions.ref assertion-diagnostics.out

#
# Verify mutex, readable-without-lock, and scheme data-policy interaction.
#
run_capture "mutex data policy diagnostics" data-policy-diagnostics.out \
    "$LOCKLINT" --check-locks data-policy.c
compare "mutex data policy diagnostics" data-policy.ref \
    data-policy-diagnostics.out

#
# Verify caller lock state through direct, wrapped, and recursive calls.
#
run_capture "basic call protection diagnostics" calls-basic-diagnostics.out \
    "$LOCKLINT" --check-locks calls-basic.c
require_match "unlocked direct call" \
    "calls-basic.c:49:22: warning: locklint: protected member 'direct_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked wrapped call" \
    "calls-basic.c:55:22: warning: locklint: protected member 'transitive_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked recursive call" \
    "calls-basic.c:69:22: warning: locklint: protected member 'recursive_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked first aggregate leaf" \
    "calls-basic.c:128:14: warning: locklint: protected member 'pair.first' modified without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked second aggregate leaf" \
    "calls-basic.c:128:14: warning: locklint: protected member 'pair.second' modified without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
if [ "$(grep -c 'warning:' calls-basic-diagnostics.out)" -ne 5 ]; then
	fail "basic call protection diagnostics: expected exactly five warnings"
fi

#
# Verify exact computed object identities for common alias forms.
#
run_capture "computed object alias diagnostics" identity-aliases-diagnostics.out \
    "$LOCKLINT" --check-locks identity-aliases.c
require_match "different copied pointer" \
    "identity-aliases.c:83:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different constant array element" \
    "identity-aliases.c:115:26: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different symbolic array element" \
    "identity-aliases.c:138:31: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different recovered container" \
    "identity-aliases.c:173:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
if [ "$(grep -c 'warning:' identity-aliases-diagnostics.out)" -ne 4 ]; then
	fail "computed object alias diagnostics: expected exactly four warnings"
fi

#
# Verify formal-to-actual identity for same and different caller objects.
#
run_capture "same formal actual identities" identity-formals-same.out \
    "$LOCKLINT" --check-locks identity-formals.c
require_empty "same formal actual identities" identity-formals-same.out

run_capture "different formal actual identities" identity-formals-different.out \
    "$LOCKLINT" -DFORMAL_ALIAS_DIFFERENT --check-locks identity-formals.c
require_match "different direct and wrapped formal actual" \
    "identity-formals.c:57:21: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-formals-different.out
require_match "different independent formal actual" \
    "identity-formals.c:90:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-formals-different.out
if [ "$(grep -c 'warning:' identity-formals-different.out)" -ne 2 ]; then
	fail "different formal actual identities: expected exactly two warnings"
fi

#
# Verify state and identity propagation across translation units.
#
run_capture "cross translation unit diagnostics" cross-diagnostics.out \
    "$LOCKLINT" --check-locks cross-caller.c cross-callee.c
require_match "cross translation unlocked access" \
    "cross-callee.c:25:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    cross-diagnostics.out
require_match "cross translation held on return" \
    "cross-callee.c:31:22: warning: locklint: lock 'lock' held on return from 'cross_acquire' \\[lock-held-on-return\\]" \
    cross-diagnostics.out
if [ "$(grep -c 'warning:' cross-diagnostics.out)" -ne 2 ]; then
	fail "cross translation unit diagnostics: expected exactly two warnings"
fi

#
# Verify protected access through an exactly resolved indirect call.
#
run_capture "exact indirect call diagnostics" indirect-call-diagnostics.out \
    "$LOCKLINT" --check-locks indirect-calls.c
require_match "unlocked exact indirect call" \
    "indirect-calls.c:43:22: warning: locklint: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    indirect-call-diagnostics.out
if [ "$(grep -c 'warning:' indirect-call-diagnostics.out)" -ne 1 ]; then
	fail "exact indirect call diagnostics: expected exactly one warning"
fi

#
# Verify NOT_REACHED removes terminated paths from lock-state merges.
#
run_capture "not reached diagnostics" not-reached-diagnostics.out \
    "$LOCKLINT" --check-locks not-reached.c
require_match "live unlocked path diagnostic" \
    "not-reached.c:79:14: warning: locklint: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
    not-reached-diagnostics.out
if [ "$(grep -c 'warning:' not-reached-diagnostics.out)" -ne 1 ]; then
	fail "not reached diagnostics: expected exactly one warning"
fi

#
# Verify the initial context walk seeds roots, stabilizes CFG loops, and
# publishes a function exit without emitting locking diagnostics.
#
run_capture "context counting" context-counting.out \
    "$LOCKLINT" --dump-contexts context-counting.c
require_match "context counting" '^roots 1$' context-counting.out
require_match "context counting" '^functions 2$' context-counting.out
require_match "context counting" '^contexts created 2 reused 0$' \
    context-counting.out
require_match "context counting" '^point-states created 32 reused 2$' \
    context-counting.out
require_match "context counting" '^exits created 2 reused 0$' \
    context-counting.out
require_match "context counting" '^continuations created 1 reused 0$' \
    context-counting.out
require_match "context counting" '^provenance-edges created 1 reused 0$' \
    context-counting.out
require_match "context counting" '^reactivations 1$' context-counting.out
require_match "context counting" \
    '^lock-identities created 0 reused 0 unresolved 0 retained 0$' \
    context-counting.out
reject_match "context counting" '^statistics ' context-counting.out

run_capture "context statistics" context-statistics.out \
    "$LOCKLINT" --dump-contexts --dump-statistics context-counting.c
require_match "context counting call bindings find" \
    '^statistics call_binding_environments_find 1$' context-statistics.out
require_match "context counting call contexts find" \
    '^statistics call_contexts_find 1$' context-statistics.out
require_match "context counting CFG point states find" \
    '^statistics cfg_point_states_find 32$' context-statistics.out
require_match "context counting backedge point states find" \
    '^statistics backedge_point_states_find 1$' context-statistics.out
require_match "context counting call exit point states find" \
    '^statistics call_exit_point_states_find 1$' context-statistics.out
require_match "context counting continuation enumeration" \
    '^statistics exit_publication_continuations_enum 2$' \
    context-statistics.out
require_match "context counting protected context enumeration" \
    '^statistics protected_contexts_enum 2$' context-statistics.out
require_match "context counting protected point enumeration" \
    '^statistics protected_scan_point_states_enum 2$' context-statistics.out
for statistic in \
    call_binding_environments_find \
    root_binding_environments_find \
    effect_binding_environments_find \
    call_contexts_find \
    root_contexts_find \
    effect_contexts_find \
    cfg_point_states_find \
    backedge_point_states_find \
    backedge_record_point_states_find \
    call_exit_point_states_find \
    call_import_semantic_states_find \
    call_exit_semantic_states_find \
    lock_transition_semantic_states_find \
    conditional_lock_semantic_states_find \
    lock_assertion_semantic_states_find \
    visibility_transition_semantic_states_find \
    competition_transition_semantic_states_find \
    backedge_widening_semantic_states_find \
    root_semantic_states_find \
    effect_semantic_states_find \
    call_continuations_find \
    call_provenance_edges_find \
    declared_effect_contexts_enum \
    lock_transition_contexts_enum \
    declared_order_contexts_enum \
    lock_assertion_contexts_enum \
    competition_underflow_contexts_enum \
    competition_effect_contexts_enum \
    competition_assertion_contexts_enum \
    protected_contexts_enum \
    assumed_call_contexts_enum \
    local_return_contexts_enum \
    caller_return_contexts_enum \
    measurement_contexts_enum \
    cleanup_contexts_enum \
    backedge_point_states_enum \
    lock_transition_point_states_enum \
    declared_order_point_states_enum \
    lock_assertion_point_states_enum \
    competition_underflow_point_states_enum \
    competition_assertion_point_states_enum \
    protected_scan_point_states_enum \
    protected_policy_states_enum \
    assumed_call_point_states_enum \
    local_return_point_states_enum \
    caller_return_point_states_enum \
    measurement_point_states_enum \
    cleanup_point_states_enum \
    measurement_binding_environments_enum \
    cleanup_binding_environments_enum \
    measurement_semantic_states_enum \
    cleanup_semantic_states_enum \
    exit_publication_continuations_enum \
    cleanup_continuations_enum \
    caller_recovery_provenance_edges_enum \
    cleanup_provenance_edges_enum \
    caller_recovery_requests \
    caller_recovery_unique_starts \
    caller_recovery_contexts_visited \
    caller_recovery_unique_contexts_visited \
    caller_recovery_edges_examined \
    caller_recovery_root_calls \
    type_registration_symbols_visited \
    type_registration_nodes_visited \
    type_registry_find \
    type_registry_duplicates \
    type_registry_insertions \
    type_registry_comparisons
do
	require_match "context statistic $statistic" \
	    "^statistics $statistic [0-9][0-9]*$" context-statistics.out
done
if [ "$(grep -c '^statistics [a-z_]* [0-9][0-9]*$' \
    context-statistics.out)" -ne 68 ]; then
	fail "context statistics: expected exactly sixty-eight statistics lines"
fi
for histogram in \
    caller_recovery_first_max_depth \
    caller_recovery_repeat_max_depth \
    caller_recovery_first_contexts_visited \
    caller_recovery_repeat_contexts_visited \
    caller_recovery_first_edges_examined \
    caller_recovery_repeat_edges_examined \
    caller_recovery_first_root_calls \
    caller_recovery_repeat_root_calls
do
	statistics_histogram_pattern="^statistics distribution $histogram "\
"samples [0-9][0-9]* total [0-9][0-9]* max [0-9][0-9]*$"
	require_match "context statistics histogram $histogram" \
	    "$statistics_histogram_pattern" context-statistics.out
done
reject_match "context counting" 'warning:' context-counting.out

#
# Verify resolved calls reuse semantic contexts, distinguish same-actual from
# distinct-actual pointer bindings, and terminate through direct and
# mutual-recursion dependency cycles.
#
run_capture "context calls" context-calls.out \
    "$LOCKLINT" --dump-contexts context-calls.c
require_match "context calls" '^roots 1$' context-calls.out
require_match "context calls" '^functions 7$' context-calls.out
require_match "context calls" '^semantic-states created 7 reused 22$' \
    context-calls.out
require_match "context calls" \
    '^binding-environments created 9 reused 6$' context-calls.out
require_match "context calls" '^binding-identities composed 4$' \
    context-calls.out
require_match "context calls" '^contexts created 9 reused 6$' \
    context-calls.out
require_match "context calls" '^point-states created 82 reused 3$' \
    context-calls.out
require_match "context calls" '^exits created 9 reused 0$' \
    context-calls.out
require_match "context calls" '^continuations created 14 reused 0$' \
    context-calls.out
require_match "context calls" '^provenance-edges created 14 reused 0$' \
    context-calls.out
require_match "context calls" '^reactivations 14$' context-calls.out
require_match "context calls" \
    '^return-states mapped 14 locks-filtered 0$' context-calls.out
require_match "context calls" '^worklist peak 3$' context-calls.out
require_match "context calls" \
    '^lock-identities created 4 reused 10 unresolved 0 retained 4$' \
    context-calls.out
require_match "context calls" \
    '^lock-identity-types unspecified 0 object 0 symbol 0 pseudo 4$' \
    context-calls.out
require_match "context calls" '^lock-identity-analysis-objects 2$' \
    context-calls.out
require_match "context calls" \
    '^distribution contexts/function samples 7 total 9 max 2$' \
    context-calls.out
require_match "context calls" \
    '^distribution binding-environments/function samples 7 total 9 max 2$' \
    context-calls.out
require_match "context calls" \
    '^distribution bindings/environment samples 9 total 8 max 2$' \
    context-calls.out
require_match "context calls" \
    '^distribution semantic-states/function samples 7 total 7 max 1$' \
    context-calls.out
require_match "context calls" \
    '^distribution point-states/context samples 9 total 82 max 19$' \
    context-calls.out
require_match "context calls histogram maximum bar" \
    '^           4-7 |\*\{40\}| 4$' context-calls.out
require_match "context calls histogram scaled bar" \
    '^         16-31 |\*\{10\}                              | 1$' \
    context-calls.out
require_match "context calls" \
    '^distribution states/analysis-point samples 82 total 82 max 1$' \
    context-calls.out
require_match "context calls" \
    '^distribution exits/context samples 9 total 9 max 1$' \
    context-calls.out
require_match "context calls" \
    '^distribution continuations/context samples 9 total 14 max 3$' \
    context-calls.out
require_match "context calls" \
    '^distribution provenance-edges/context samples 9 total 14 max 3$' \
    context-calls.out
require_match "context calls" \
    '^maximum contexts/function 2 function binding_leaf tu=context-calls.c$' \
    context-calls.out
require_match "context calls" \
    '^maximum binding-environments/function 2 function binding_leaf tu=context-calls.c$' \
    context-calls.out
require_match "context calls" \
    '^maximum bindings/environment 2 function binding_leaf tu=context-calls.c$' \
    context-calls.out
require_match "context calls" \
    '^maximum continuations/context 3 function binding_leaf tu=context-calls.c$' \
    context-calls.out
require_match "context calls" \
    '^maximum provenance-edges/context 3 function binding_leaf tu=context-calls.c$' \
    context-calls.out
reject_match "context calls" '^linear-lookup ' context-calls.out
require_match "context calls" \
    '^distribution locks/semantic-state samples 7 total 0 max 0$' \
    context-calls.out
require_match "context calls" \
    '^distribution visibility/semantic-state samples 7 total 0 max 0$' \
    context-calls.out
require_match "context calls" \
    '^memory binding-environments [1-9][0-9]* bytes$' context-calls.out
require_match "context calls" '^memory retained-collections [1-9][0-9]* bytes$' \
    context-calls.out
reject_match "context calls" 'warning:' context-calls.out

#
# Final report
#
if [ "$failures" -ne 0 ]; then
	echo "$failures locklint test(s) failed" >&2
	exit 1
fi

echo "All locklint tests passed"

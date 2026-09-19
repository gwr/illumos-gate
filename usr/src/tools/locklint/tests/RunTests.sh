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
    '^lock-transitions applied 8 deferred 1$' lock-identity-local.out
require_match "local lock returns" \
    '^return-states mapped 8 locks-filtered 2$' lock-identity-local.out
require_match "local lock contexts" \
    '^contexts created 7 reused 1$' lock-identity-local.out
require_match "local lock states" \
    '^distribution semantic-states/function samples 6 total 12 max 2 bins 0:0 1:0 2:6 3:0 4:0 5:0 6-8:0 9+:0$' \
    lock-identity-local.out
require_match "local held-lock states" \
    '^distribution locks/semantic-state samples 12 total 6 max 1 bins 0:6 1:6 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
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
require_match "local held on return" \
    "lock-identity-local.c:53:22: warning: locklint: lock 'local_lock' held on return from 'local_lock_helper' \\[lock-held-on-return\\]" \
    lock-identity-local.out
require_match "local maybe held on return" \
    "lock-identity-local.c:62:30: warning: locklint: lock 'local_lock' held on only some paths returning from 'local_maybe_lock_helper' \\[lock-maybe-held-on-return\\]" \
    lock-identity-local.out
if [ "$(grep -c 'warning:' lock-identity-local.out)" -ne 4 ]; then
	fail "local lock diagnostics: expected exactly four warnings"
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
if [ "$(grep -c 'warning:' lock-transition-diagnostics.out)" -ne 6 ]; then
	fail "lock transition diagnostics: expected exactly six warnings"
fi

#
# Verify structure-valued global and member mutex identities.
#
run_capture "structure-valued mutex diagnostics" struct-lock-diagnostics.out \
    "$LOCKLINT" --check-locks struct-lock.c
require_match "global structure-valued mutex diagnostic" \
    "struct-lock.c:47:9: warning: locklint: protected member 'global_value' modified without holding 'global_lock' \\[unprotected-access\\]" \
    struct-lock-diagnostics.out
require_match "member structure-valued mutex diagnostic" \
    "struct-lock.c:56:14: warning: locklint: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
    struct-lock-diagnostics.out
if [ "$(grep -c 'warning:' struct-lock-diagnostics.out)" -ne 2 ]; then
	fail "structure-valued mutex diagnostics: expected exactly two warnings"
fi

#
# Verify mutex, readable-without-lock, and scheme data-policy interaction.
#
run_capture "mutex data policy diagnostics" data-policy-diagnostics.out \
    "$LOCKLINT" --check-locks data-policy.c
require_match "unprotected policy read" \
    "data-policy.c:78:22: warning: locklint: protected member 'protected' read without holding 'lock' \\[unprotected-access\\]" \
    data-policy-diagnostics.out
require_match "unprotected policy write" \
    "data-policy.c:79:14: warning: locklint: protected member 'protected' modified without holding 'lock' \\[unprotected-access\\]" \
    data-policy-diagnostics.out
require_match "readable policy write" \
    "data-policy.c:82:14: warning: locklint: protected member 'readable' modified without holding 'lock' \\[unprotected-access\\]" \
    data-policy-diagnostics.out
require_match "replacement policy read" \
    "data-policy.c:90:23: warning: locklint: protected member 'mutex_after_scheme' read without holding 'lock' \\[unprotected-access\\]" \
    data-policy-diagnostics.out
require_match "replacement policy write" \
    "data-policy.c:91:14: warning: locklint: protected member 'mutex_after_scheme' modified without holding 'lock' \\[unprotected-access\\]" \
    data-policy-diagnostics.out
if [ "$(grep -c 'warning:' data-policy-diagnostics.out)" -ne 5 ]; then
	fail "mutex data policy diagnostics: expected exactly five warnings"
fi

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
# Verify NOT_REACHED removes terminated paths from lock-state merges.
#
run_capture "not reached diagnostics" not-reached-diagnostics.out \
    "$LOCKLINT" --check-locks not-reached.c
require_match "live unlocked path diagnostic" \
    "not-reached.c:54:14: warning: locklint: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
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
    '^distribution contexts/function samples 7 total 9 max 2 bins 0:0 1:5 2:2 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution binding-environments/function samples 7 total 9 max 2 bins 0:0 1:5 2:2 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution bindings/environment samples 9 total 8 max 2 bins 0:5 1:0 2:4 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution semantic-states/function samples 7 total 7 max 1 bins 0:0 1:7 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution point-states/context samples 9 total 82 max 19 bins 0:0 1:0 2:0 3:1 4:0 5:4 6-8:0 9+:4$' \
    context-calls.out
require_match "context calls" \
    '^distribution states/analysis-point samples 82 total 82 max 1 bins 0:0 1:82 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution exits/context samples 9 total 9 max 1 bins 0:0 1:9 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution continuations/context samples 9 total 14 max 3 bins 0:1 1:3 2:4 3:1 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution provenance-edges/context samples 9 total 14 max 3 bins 0:1 1:3 2:4 3:1 4:0 5:0 6-8:0 9+:0$' \
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
    '^distribution locks/semantic-state samples 7 total 0 max 0 bins 0:7 1:0 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
    context-calls.out
require_match "context calls" \
    '^distribution visibility/semantic-state samples 7 total 0 max 0 bins 0:7 1:0 2:0 3:0 4:0 5:0 6-8:0 9+:0$' \
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

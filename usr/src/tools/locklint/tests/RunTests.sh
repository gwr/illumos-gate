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
COMPARE_LOCK_REPORTS=./compare-lock-reports.py
COMPARE_PROTECTION_REPORTS=./compare-protection-reports.py
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
# Locklint findings are the default diagnostic output.  Ordinary parser
# warnings are opt-in, while parser errors remain visible.
#
run_capture "hidden parser warnings" parser-warning-hidden.out \
    "$LOCKLINT" --no-check parser-warning.c
reject_match "hidden parser warning" "locklint parser warning test" \
    parser-warning-hidden.out

run_capture "enabled parser warnings" parser-warning-enabled.out \
    "$LOCKLINT" --no-check --parser-warnings parser-warning.c
require_match "enabled parser warning" "locklint parser warning test" \
    parser-warning-enabled.out

run_failure "visible parser errors" parser-error.out \
    "$LOCKLINT" --no-check parser-error.c
require_match "visible parser error" "locklint parser error test" \
    parser-error.out

#
# Dump streams have one identifying header.  Per-function dumps sharing
# stdout are collected into complete sections, while an explicit pathname
# receives only its selected dump.
#
run_capture "separated stdout dumps" dump-sections.out \
    "$LOCKLINT" --no-check --dump-linearized --dump-accesses events.c
for dump in linearized accesses
do
	require_match "dump-$dump header" "^#### dump-$dump ####$" \
	    dump-sections.out
	if [ "$(grep -c "^#### dump-$dump ####$" dump-sections.out)" -ne 1 ]
	then
		fail "dump-$dump header: expected exactly one header"
	fi
done
linearized_header=$(grep -n '^#### dump-linearized ####$' \
    dump-sections.out | cut -d: -f1)
accesses_header=$(grep -n '^#### dump-accesses ####$' \
    dump-sections.out | cut -d: -f1)
if [ "$linearized_header" -ge "$accesses_header" ]; then
	fail "separated stdout dumps: unexpected section order"
fi

run_capture "explicit dump output" dump-accesses-stdout.out \
    "$LOCKLINT" --no-check --dump-accesses=dump-accesses-file.out events.c
require_empty "explicit dump stdout" dump-accesses-stdout.out
require_match "explicit dump header" "^#### dump-accesses ####$" \
    dump-accesses-file.out
require_match "explicit dump content" " function=" dump-accesses-file.out

run_capture "explicit whole-program dump" dump-callgraph-stdout.out \
    "$LOCKLINT" --no-check \
    --dump-callgraph=dump-callgraph-file.out events.c
require_empty "explicit whole-program dump stdout" dump-callgraph-stdout.out
require_match "explicit whole-program dump header" \
    "^#### dump-callgraph ####$" dump-callgraph-file.out
require_match "explicit whole-program dump content" "^function " \
    dump-callgraph-file.out

run_failure "empty dump pathname" dump-empty-path.out \
    "$LOCKLINT" --dump-accesses= events.c
require_match "empty dump pathname diagnostic" \
    "dump-accesses requires a non-empty pathname" dump-empty-path.out

run_failure "shared dump pathname" dump-shared-path.out \
    "$LOCKLINT" --dump-accesses=dump-shared-file.out \
    --dump-events=dump-shared-file.out events.c
require_match "shared dump pathname diagnostic" \
    "dump-accesses and dump-events cannot share output pathname" \
    dump-shared-path.out

run_capture "all dump headers" dump-all-headers.out \
    "$LOCKLINT" --no-check --dump-all events.c
for dump in parsed linearized accesses annotations events callgraph contexts \
    protection-states statistics types
do
	require_match "dump-all $dump header" "^#### dump-$dump ####$" \
	    dump-all-headers.out
	if [ "$(grep -c "^#### dump-$dump ####$" dump-all-headers.out)" -ne 1 ]
	then
		fail "dump-all $dump header: expected exactly one header"
	fi
done

#
# Verify cross-analyzer protected-access comparison.  Duplicate counts are
# informational, while every operation/data/lock group must occur on both
# sides.
#
run_capture "equivalent lock reports" compare-lock-reports.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-newll.in
require_match "equivalent lock report count difference" \
    "count differs: read of 'flags' requiring 'lock' in sample.c (OSLL 3, new locklint 2)" \
    compare-lock-reports.out
require_match "equivalent lock report result" \
    "equivalent: 3 protected-access groups occur on both sides" \
    compare-lock-reports.out

run_capture "normalized OSLL reference" compare-lock-reports-reference.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.ref \
    --from-newll=compare-lock-reports-newll.in
require_match "normalized OSLL reference result" \
    "equivalent: 3 protected-access groups occur on both sides" \
    compare-lock-reports-reference.out

run_capture "write normalized OSLL reference" \
    compare-lock-reports-write-reference.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.in \
    --osll-reference-output=compare-lock-reports-written.ref
if ! cmp -s compare-lock-reports-osll.ref compare-lock-reports-written.ref
then
	fail "write normalized OSLL reference: output differs"
fi
rm -f compare-lock-reports-written.ref

run_failure "strict lock report locations" \
    compare-lock-reports-locations.out \
    "$COMPARE_LOCK_REPORTS" --match=location \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-newll.in
require_match "missing strict lock report location" \
    "missing from new locklint: read of 'flags' requiring 'lock' at sample.c:11" \
    compare-lock-reports-locations.out

run_capture "normalized lock reports" compare-lock-reports-normalized.out \
    "$COMPARE_LOCK_REPORTS" \
    --normalized-output=compare-lock-reports-normalized.tsv \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-newll.in
require_match "normalized lock report header" \
    "^analyzer	source	line	column	function	operation	datum	lock$" \
    compare-lock-reports-normalized.tsv
require_match "normalized OSLL lock report" \
    "^osll	sample.c	10		sample	read	flags	lock$" \
    compare-lock-reports-normalized.tsv
require_match "normalized new locklint report" \
    "^new-locklint	sample.c	10	1		read	flags	lock$" \
    compare-lock-reports-normalized.tsv
if [ "$(wc -l < compare-lock-reports-normalized.tsv)" -ne 10 ]; then
	fail "normalized lock reports: expected exactly ten lines"
fi

run_failure "missing new locklint report" compare-lock-reports-missing.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-missing.in
require_match "missing new locklint report" \
    "missing from new locklint: read of 'flags' requiring 'lock'" \
    compare-lock-reports-missing.out

run_failure "unexpected new locklint report" \
    compare-lock-reports-unexpected.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-unexpected.in
require_match "unexpected new locklint report" \
    "unexpected in new locklint: read of 'extra' requiring 'lock'" \
    compare-lock-reports-unexpected.out

run_capture "OSLL lock report coverage" \
    compare-lock-reports-covered.out \
    "$COMPARE_LOCK_REPORTS" --expect=osll-covered \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-unexpected.in
require_match "OSLL lock report coverage result" \
    "covered: 3 OSLL protected-access groups occur in new locklint; 1 new locklint-only group" \
    compare-lock-reports-covered.out

run_capture "reviewed native-only lock report" \
    compare-lock-reports-reviewed.out \
    "$COMPARE_LOCK_REPORTS" --expect=osll-covered \
    --native-only-reference=compare-lock-reports-native-only.ref \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-unexpected.in
require_match "reviewed native-only lock report result" \
    "covered: 3 OSLL protected-access groups occur in new locklint; 1 reviewed native-only group" \
    compare-lock-reports-reviewed.out

run_failure "unreviewed native-only lock report" \
    compare-lock-reports-unreviewed.out \
    "$COMPARE_LOCK_REPORTS" --expect=osll-covered \
    --native-only-reference=compare-lock-reports-no-native-only.ref \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-unexpected.in
require_match "unreviewed native-only lock report" \
    "unreviewed in new locklint: read of 'extra' requiring 'lock'" \
    compare-lock-reports-unreviewed.out

run_failure "stale native-only lock report" \
    compare-lock-reports-stale.out \
    "$COMPARE_LOCK_REPORTS" --expect=osll-covered \
    --native-only-reference=compare-lock-reports-native-only.ref \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-newll.in
require_match "stale native-only lock report" \
    "reviewed native-only group no longer reported: read of 'extra' requiring 'lock'" \
    compare-lock-reports-stale.out

run_failure "insufficient new locklint locations" \
    compare-lock-reports-insufficient.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-osll.in \
    --from-newll=compare-lock-reports-insufficient.in
require_match "insufficient new locklint locations" \
    "insufficient new locklint locations: read of 'flags' requiring 'lock' in sample.c (OSLL functions 2, new locklint locations 1)" \
    compare-lock-reports-insufficient.out

run_failure "malformed OSLL lock report" compare-lock-reports-malformed.out \
    "$COMPARE_LOCK_REPORTS" \
    --from-osll=compare-lock-reports-malformed.in \
    --from-newll=compare-lock-reports-newll.in
require_match "malformed OSLL lock report" \
    "incomplete OSLL protected-access report: missing protector" \
    compare-lock-reports-malformed.out

#
# Verify cross-analyzer observed-protection comparison.  Native detail is
# retained while comparison uses the common facts exposed by OSLL vars -h.
#
run_capture "equivalent protection reports" compare-protection-reports.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --from-osll=compare-protection-reports-osll.in \
    --from-newll=compare-protection-reports-newll.in
require_match "equivalent protection report result" \
    "equivalent: 8 protection records agree on shared OSLL facts" \
    compare-protection-reports.out

run_capture "normalized OSLL protection reference" \
    compare-protection-reports-reference.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --from-osll=compare-protection-reports-osll.ref \
    --from-newll=compare-protection-reports-newll.in
require_match "normalized OSLL protection reference result" \
    "equivalent: 8 protection records agree on shared OSLL facts" \
    compare-protection-reports-reference.out

run_capture "write normalized OSLL protection reference" \
    compare-protection-reports-write-reference.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --from-osll=compare-protection-reports-osll.in \
    --osll-reference-output=compare-protection-reports-written.ref
if ! cmp -s compare-protection-reports-osll.ref \
    compare-protection-reports-written.ref
then
	fail "write normalized OSLL protection reference: output differs"
fi
rm -f compare-protection-reports-written.ref

run_capture "normalized protection reports" \
    compare-protection-reports-normalized.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --normalized-output=compare-protection-reports-normalized.tsv \
    --from-osll=compare-protection-reports-osll.in \
    --from-newll=compare-protection-reports-newll.in
require_match "normalized protection report header" \
    "^analyzer	original_datum	datum	write_state	common_state	common_locks	observed_locks	protection	notes$" \
    compare-protection-reports-normalized.tsv
require_match "normalized OSLL protection report" \
    "^osll	sample.c:one	sample.c::one	unknown	locks	sample.c::common_lock	sample.c::common_lock		$" \
    compare-protection-reports-normalized.tsv
require_match "normalized unobserved OSLL protection report" \
    "^osll	sample.c:unobserved	sample.c::unobserved	unknown	unobserved				$" \
    compare-protection-reports-normalized.tsv
require_match "normalized native protection detail" \
    "^new-locklint	sample.c::mixed	sample.c::mixed	yes	empty		sample.c::common_lock	mutex	note1,note4$" \
    compare-protection-reports-normalized.tsv
if [ "$(wc -l < compare-protection-reports-normalized.tsv)" -ne 18 ]; then
	fail "normalized protection reports: expected exactly 18 lines"
fi

run_capture "native protection report vocabulary" \
    compare-protection-reports-vocabulary.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --normalized-output=compare-protection-reports-vocabulary.tsv \
    --from-osll=compare-protection-reports-vocabulary-osll.in \
    --from-newll=compare-protection-reports-vocabulary-newll.in
require_match "native protection report vocabulary result" \
    "equivalent: 6 protection records agree on shared OSLL facts" \
    compare-protection-reports-vocabulary.out
if ! cmp -s compare-protection-reports-vocabulary.ref \
    compare-protection-reports-vocabulary.tsv
then
	fail "native protection report vocabulary: normalized output differs"
fi
rm -f compare-protection-reports-vocabulary.tsv

run_failure "different protection reports" \
    compare-protection-reports-different.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --from-osll=compare-protection-reports-osll.in \
    --from-newll=compare-protection-reports-different.in
require_match "protection report write difference" \
    "write state differs for 'sample.c::unlocked': OSLL yes, new locklint no" \
    compare-protection-reports-different.out
require_match "protection report lock difference" \
    "common locks differ for 'sample.c::one': OSLL sample.c::common_lock, new locklint sample.c::other_lock" \
    compare-protection-reports-different.out
require_match "protection report unresolved difference" \
    "new locklint protection unresolved for 'sample.c::mixed'" \
    compare-protection-reports-different.out
require_match "missing native protection report" \
    "missing from new locklint: protection data 'sample_state::field'" \
    compare-protection-reports-different.out
require_match "unexpected native protection report" \
    "unexpected in new locklint: protection data 'sample.c::extra'" \
    compare-protection-reports-different.out

run_failure "malformed OSLL protection report" \
    compare-protection-reports-malformed.out \
    "$COMPARE_PROTECTION_REPORTS" \
    --from-osll=compare-protection-reports-malformed.in \
    --from-newll=compare-protection-reports-newll.in
require_match "malformed OSLL protection report" \
    "unrecognized OSLL protection report" \
    compare-protection-reports-malformed.out

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
run_capture "events" events.out \
    "$LOCKLINT" --no-check --dump-events events.c
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
    '^lock-identities created 3 reused 6 unresolved 0 retained 3$' \
    lock-identity-local.out
require_match "local lock identity type" \
    '^lock-identity-types unspecified 0 object 0 symbol 3 pseudo 0$' \
    lock-identity-local.out
require_match "local lock identity objects" \
    '^lock-identity-analysis-objects 3$' lock-identity-local.out
require_match "local lock transitions" \
    '^lock-transitions applied 7 deferred 0$' lock-identity-local.out
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
    "lock-identity-local.c:79:19: warning: lock 'local_lock' is not held \\[lock-not-held\\]" \
    lock-identity-local.out
require_match "local duplicate acquire" \
    "lock-identity-local.c:81:20: warning: lock 'local_lock' is already held \\[lock-already-held\\]" \
    lock-identity-local.out
require_match "formal held on return" \
    "lock-identity-local.c:39:21: warning: lock 'lock' held on return from 'acquire_helper' \\[lock-held-on-return\\]" \
    lock-identity-local.out
require_match "local held on return" \
    "lock-identity-local.c:53:22: warning: lock 'local_lock' held on return from 'local_lock_helper' \\[lock-held-on-return\\]" \
    lock-identity-local.out
require_match "local maybe held on return" \
    "lock-identity-local.c:62:30: warning: lock 'local_lock' held on only some paths returning from 'local_maybe_lock_helper' \\[lock-maybe-held-on-return\\]" \
    lock-identity-local.out
reject_match "local terminated nested acquisition path" \
    "lock-identity-local.c:83:32:" \
    lock-identity-local.out
if [ "$(grep -c 'warning:' lock-identity-local.out)" -ne 5 ]; then
	fail "local lock diagnostics: expected exactly five warnings"
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
# Pointer-valued members retain one lock identity while their value is
# unchanged.  Explicit reassignment must keep the old and new locks distinct.
#
run_capture "pointer member lock identities" \
    lock-identity-pointer-member.out \
    "$LOCKLINT" lock-identity-pointer-member.c
reject_match "repeated pointer member lock identity" \
    "lock-identity-pointer-member.c:50:19:.*\\[lock-not-held\\]" \
    lock-identity-pointer-member.out
reject_match "nested pointer alias lock identity" \
    "lock-identity-pointer-member.c:59:19:.*\\[lock-not-held\\]" \
    lock-identity-pointer-member.out
require_match "reassigned pointer member lock identity" \
    "lock-identity-pointer-member.c:68:19: warning: lock 'lock' is not held \\[lock-not-held\\]" \
    lock-identity-pointer-member.out
if [ "$(grep -c 'warning:' lock-identity-pointer-member.out)" -ne 1 ]; then
	fail "pointer member lock identities: expected exactly one warning"
fi

#
# Pointer-member locks derived from a call result remain stable through a
# balanced drop/reacquire loop.  A missing reacquisition remains visible.
#
run_capture "call-result lock identities" lock-identity-call-result.out \
    "$LOCKLINT" lock-identity-call-result.c
reject_match "balanced call-result loop release" \
    "lock-identity-call-result.c:57:35:.*\\[lock-not-held\\]" \
    lock-identity-call-result.out
reject_match "balanced call-result loop reacquisition" \
    "lock-identity-call-result.c:59:36:.*\\[lock-maybe-already-held\\]" \
    lock-identity-call-result.out
reject_match "balanced call-result loop final release" \
    "lock-identity-call-result.c:64:19:.*\\[lock-not-held\\]" \
    lock-identity-call-result.out
require_match "missing loop reacquisition body" \
    "lock-identity-call-result.c:76:35: warning: lock 'lock' may not be held \\[lock-maybe-not-held\\]" \
    lock-identity-call-result.out
require_match "missing loop reacquisition final release" \
    "lock-identity-call-result.c:82:19: warning: lock 'lock' may not be held \\[lock-maybe-not-held\\]" \
    lock-identity-call-result.out
if [ "$(grep -c 'warning:' lock-identity-call-result.out)" -ne 2 ]; then
	fail "call-result lock identities: expected exactly two warnings"
fi

#
# A lock acquired on a callee-local object and returned as a side effect must
# be visible through the caller's call-result identity.
#
run_capture "return object lock binding" return-bindings.out \
    "$LOCKLINT" return-bindings.c
reject_match "return object lock binding" 'warning:' return-bindings.out

run_capture "return object edge binding" return-bindings-edge.out \
    "$LOCKLINT" --dump-contexts return-bindings-edge.c
reject_match "return object edge binding" 'warning:' \
    return-bindings-edge.out
require_match "discarded return result remains mapped" \
    '^return-states mapped 7 locks-filtered 0$' return-bindings-edge.out
require_match "return aliases remain bounded" \
    '^distribution alias-entries/set samples 10 total 4 max 1$' \
    return-bindings-edge.out

#
# Verify preprocessing-time annotation capture and initial name resolution.
#
run_capture "annotations" annotations.out \
    "$LOCKLINT" --dump-annotations annotations.c
compare "annotations" annotations.ref annotations.out

run_capture "declared effect annotations" declared-effect-annotations.out \
    "$LOCKLINT" --no-check --dump-annotations declared-effect-annotations.c
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
    "$LOCKLINT" --no-check --dump-annotations rwlock-covers-locks.c
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
    "$LOCKLINT" lock-order-cycle.c
compare "declared lock order cycle" lock-order-cycle.ref \
    lock-order-cycle.out

#
# Verify post-parse command-file lock-order declarations, including adjacent
# edges, duplicate declarations, source/command cycles, and resolution errors.
#
run_capture "command lock order annotations" command-lock-order.out \
    "$LOCKLINT" --no-check --dump-annotations \
    --cf command-lock-order.cf command-lock-order.c
require_match "command lock order mixed names" \
    "command-lock-order.cf:2: LOCK_ORDER command_order_global -> command_order_object.first -> command_order_state::third" \
    command-lock-order.out
if [ "$(grep -c \
    'command-lock-order.cf:.*LOCK_ORDER command_order_state::first -> command_order_state::second' \
    command-lock-order.out)" -ne 2 ]; then
	fail "command lock order annotations: expected two duplicate command declarations"
fi
require_match "command lock order source duplicate" \
    "command-lock-order.c:37:1: LOCK_ORDER command_order_state::first -> command_order_state::second" \
    command-lock-order.out

run_capture "command lock order equivalent types" \
    command-lock-order-equivalent.out "$LOCKLINT" \
    --no-check --dump-annotations \
    --cf command-lock-order-equivalent.cf readable.c readable-other.c
require_match "command lock order equivalent types" \
    "command-lock-order-equivalent.cf:1: LOCK_ORDER command_global_lock -> duplicate_command_type::value" \
    command-lock-order-equivalent.out

run_capture "command lock order violation" \
    command-lock-order-violation.out "$LOCKLINT" \
    --cf command-lock-order-violation.cf command-lock-order.c
compare "command lock order violation" command-lock-order-violation.ref \
    command-lock-order-violation.out

run_capture "command lock order cycle" command-lock-order-cycle.out \
    "$LOCKLINT" --cf command-lock-order-cycle.cf command-lock-order.c
compare "command lock order cycle" command-lock-order-cycle.ref \
    command-lock-order-cycle.out

run_failure "command lock order arity" command-lock-order-arity.out \
    "$LOCKLINT" --cf command-lock-order-arity.cf command-lock-order.c
require_match "command lock order arity" \
    "declare lock-order requires at least two lock names" \
    command-lock-order-arity.out

run_failure "command lock order invalid name" command-lock-order-invalid.out \
    "$LOCKLINT" --cf command-lock-order-invalid.cf command-lock-order.c
require_match "command lock order invalid name" \
    "invalid lock name 'invalid-name'" command-lock-order-invalid.out

run_failure "command lock order unresolved name" \
    command-lock-order-unresolved.out "$LOCKLINT" \
    --cf command-lock-order-unresolved.cf command-lock-order.c
require_match "command lock order unresolved name" \
    "unresolved lock name 'missing_command_lock'" \
    command-lock-order-unresolved.out

run_failure "command lock order inconsistent type" \
    command-lock-order-inconsistent.out "$LOCKLINT" \
    --cf command-lock-order-inconsistent.cf \
    type-name-first.c type-name-second-different.c
require_match "command lock order inconsistent type" \
    "inconsistently defined type in lock name 'repeated_name::value'" \
    command-lock-order-inconsistent.out

run_capture "declared lock order" lock-order.out \
    "$LOCKLINT" lock-order.c
compare "declared lock order" lock-order.ref lock-order.out

run_capture "observed lock order" lock-order-observed.out \
    "$LOCKLINT" lock-order-observed.c
compare "observed lock order" lock-order-observed.ref \
    lock-order-observed.out

run_capture "conditional local declared lock order" \
    lock-order-local-conditional.out \
    "$LOCKLINT" lock-order-local-conditional.c
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
    "$LOCKLINT" --no-check --dump-annotations data-policy.c
compare "data policy annotations" data-policy-annotations.ref \
    data-policy-annotations.out

#
# Verify object-specific mutex protection declared after source parsing,
# including exact duplicates, conflicting protectors, and resolution errors.
#
run_capture "command mutex protection annotations" \
    command-mutex-protection-annotations.out "$LOCKLINT" \
    --no-check --dump-annotations \
    --cf command-mutex-protection.cf command-mutex-protection.c
require_match "command mutex member protection" \
    "command-mutex-protection.cf:2: MUTEX_PROTECTS_DATA command_mutex_object.lock -> command_mutex_object.value" \
    command-mutex-protection-annotations.out
require_match "command mutex second member protection" \
    "command-mutex-protection.cf:2: MUTEX_PROTECTS_DATA command_mutex_object.lock -> command_mutex_object.second" \
    command-mutex-protection-annotations.out
if [ "$(grep -c \
    'MUTEX_PROTECTS_DATA command_mutex_global_lock -> command_mutex_global_value' \
    command-mutex-protection-annotations.out)" -ne 2 ]; then
	fail "command mutex protection annotations: expected two duplicate global declarations"
fi
if [ "$(grep -c \
    'MUTEX_PROTECTS_DATA command_mutex_object.lock -> command_mutex_object.source_value' \
    command-mutex-protection-annotations.out)" -ne 3 ]; then
	fail "command mutex protection annotations: expected three duplicate declarations"
fi

run_capture "command mutex protection behavior" \
    command-mutex-protection.out "$LOCKLINT" \
    --dump-statistics --cf command-mutex-protection.cf \
    command-mutex-protection.c
require_match "command mutex unlocked member" \
    "protected member 'value' read without holding 'lock'.*\\[unprotected-access\\]" \
    command-mutex-protection.out
require_match "command mutex unlocked second member" \
    "protected member 'second' read without holding 'lock'.*\\[unprotected-access\\]" \
    command-mutex-protection.out
require_match "command mutex unlocked global" \
    "protected member 'command_mutex_global_value' read without holding 'command_mutex_global_lock'.*\\[unprotected-access\\]" \
    command-mutex-protection.out
if [ "$(grep -F -c '[unprotected-access]' \
    command-mutex-protection.out)" -ne 3 ]; then
	fail "command mutex protection behavior: expected three findings"
fi
require_match "command mutex duplicate semantic policy" \
    '^statistics data_policy_candidates 22$' \
    command-mutex-protection.out

run_failure "command mutex source conflict" \
    command-mutex-protection-conflict-source.out "$LOCKLINT" \
    --cf command-mutex-protection-conflict-source.cf \
    command-mutex-protection.c
require_match "command mutex source conflict command" \
    "conflicting mutex protector for data name 'command_mutex_object.source_value'" \
    command-mutex-protection-conflict-source.out
require_match "command mutex source conflict origin" \
    "previous declaration at command-mutex-protection.c:40:1" \
    command-mutex-protection-conflict-source.out

run_failure "command mutex command conflict" \
    command-mutex-protection-conflict-command.out "$LOCKLINT" \
    --cf command-mutex-protection-conflict-command.cf \
    command-mutex-protection.c
require_match "command mutex command conflict command" \
    "conflicting mutex protector for data name 'command_mutex_object.value'" \
    command-mutex-protection-conflict-command.out
require_match "command mutex command conflict origin" \
    "previous declaration at command-mutex-protection-conflict-command.cf:1" \
    command-mutex-protection-conflict-command.out

run_failure "command mutex protection arity" \
    command-mutex-protection-arity.out "$LOCKLINT" \
    --cf command-mutex-protection-arity.cf command-mutex-protection.c
require_match "command mutex protection arity" \
    "declare mutex-protects-data requires one lock and at least one data name" \
    command-mutex-protection-arity.out

run_failure "command mutex invalid lock" \
    command-mutex-protection-invalid.out "$LOCKLINT" \
    --cf command-mutex-protection-invalid.cf command-mutex-protection.c
require_match "command mutex invalid lock" \
    "invalid lock name 'invalid-name'" command-mutex-protection-invalid.out

run_failure "command mutex unresolved lock" \
    command-mutex-protection-unresolved-lock.out "$LOCKLINT" \
    --cf command-mutex-protection-unresolved-lock.cf \
    command-mutex-protection.c
require_match "command mutex unresolved lock" \
    "unresolved lock name 'missing_lock'" \
    command-mutex-protection-unresolved-lock.out

run_failure "command mutex unresolved data" \
    command-mutex-protection-unresolved-data.out "$LOCKLINT" \
    --cf command-mutex-protection-unresolved-data.cf \
    command-mutex-protection.c
require_match "command mutex unresolved data" \
    "unresolved data name 'missing_data'" \
    command-mutex-protection-unresolved-data.out

#
# Verify same-owner type-member mutex protection across separately parsed,
# layout-equivalent definitions without pairing unrelated type origins.
#
run_capture "command type mutex annotations" \
    command-type-mutex-annotations.out "$LOCKLINT" \
    --no-check --dump-annotations --cf command-type-mutex.cf \
    command-type-mutex-first.c command-type-mutex-second.c
for member in value nested.first nested.second
do
	if [ "$(grep -F -c \
	    "MUTEX_PROTECTS_DATA command_type_state::lock -> command_type_state::$member" \
	    command-type-mutex-annotations.out)" -ne 2 ]; then
		fail "command type mutex annotations: expected two $member policies"
	fi
done
if [ "$(grep -F -c \
    'MUTEX_PROTECTS_DATA command_type_state::lock -> command_type_state::source_value' \
    command-type-mutex-annotations.out)" -ne 5 ]; then
	fail "command type mutex annotations: expected five duplicate source-value declarations"
fi

run_capture "command type mutex behavior" \
    command-type-mutex.out "$LOCKLINT" \
    --cf command-type-mutex.cf \
    command-type-mutex-first.c command-type-mutex-second.c
if [ "$(grep -F -c '[unprotected-access]' \
    command-type-mutex.out)" -ne 6 ]; then
	fail "command type mutex behavior: expected six findings"
fi

run_failure "command type mutex source conflict" \
    command-type-mutex-conflict-source.out "$LOCKLINT" \
    --cf command-type-mutex-conflict-source.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex source conflict" \
    "conflicting mutex protector for data name 'command_type_state::source_value'" \
    command-type-mutex-conflict-source.out
require_match "command type mutex source conflict origin" \
    "previous declaration at command-type-mutex-first.c:23:1" \
    command-type-mutex-conflict-source.out

run_failure "command type mutex command conflict" \
    command-type-mutex-conflict-command.out "$LOCKLINT" \
    --cf command-type-mutex-conflict-command.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex command conflict" \
    "conflicting mutex protector for data name 'command_type_state::value'" \
    command-type-mutex-conflict-command.out
require_match "command type mutex command conflict origin" \
    "previous declaration at command-type-mutex-conflict-command.cf:1" \
    command-type-mutex-conflict-command.out

run_failure "command type mutex mixed scope" \
    command-type-mutex-mixed.out "$LOCKLINT" \
    --cf command-type-mutex-mixed.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex mixed scope" \
    "lock and data names must both be object-specific or type-member" \
    command-type-mutex-mixed.out

run_failure "command type mutex different owner" \
    command-type-mutex-owner.out "$LOCKLINT" \
    --cf command-type-mutex-owner.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex different owner" \
    "data name 'command_other_state::value' has a different owning type from lock name 'command_type_state::lock'" \
    command-type-mutex-owner.out

run_failure "command type mutex inconsistent type" \
    command-type-mutex-inconsistent.out "$LOCKLINT" \
    --cf command-type-mutex-inconsistent.cf \
    command-type-mutex-first.c command-type-mutex-different.c
require_match "command type mutex inconsistent type" \
    "inconsistently defined type in lock name 'command_type_state::lock'" \
    command-type-mutex-inconsistent.out

run_failure "command type mutex invalid name" \
    command-type-mutex-invalid.out "$LOCKLINT" \
    --cf command-type-mutex-invalid.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex invalid name" \
    "invalid lock name 'bad-name::lock'" command-type-mutex-invalid.out

run_failure "command type mutex unresolved member" \
    command-type-mutex-unresolved.out "$LOCKLINT" \
    --cf command-type-mutex-unresolved.cf \
    command-type-mutex-first.c command-type-mutex-second.c
require_match "command type mutex unresolved member" \
    "unresolved data name 'command_type_state::missing'" \
    command-type-mutex-unresolved.out

#
# Verify that external readers-writer protection reuses object/type
# resolution while enforcing read-held reads and write-held modifications.
#
run_capture "command rwlock protection annotations" \
    command-rwlock-protection-annotations.out "$LOCKLINT" \
    --no-check --dump-annotations \
    --cf command-rwlock-protection.cf command-rwlock-protection.c
if [ "$(grep -F -c \
    'RWLOCK_PROTECTS_DATA command_rwlock_state::lock -> command_rwlock_state::value' \
    command-rwlock-protection-annotations.out)" -ne 2 ]; then
	fail "command rwlock annotations: expected two value declarations"
fi
if [ "$(grep -F -c \
    'RWLOCK_PROTECTS_DATA command_rwlock_state::lock -> command_rwlock_state::duplicate' \
    command-rwlock-protection-annotations.out)" -ne 3 ]; then
	fail "command rwlock annotations: expected three duplicate declarations"
fi
require_match "command rwlock global annotation" \
    "command-rwlock-protection.cf:8: RWLOCK_PROTECTS_DATA command_rwlock_global_lock -> command_rwlock_global_value" \
    command-rwlock-protection-annotations.out

run_capture "command rwlock protection behavior" \
    command-rwlock-protection.out "$LOCKLINT" \
    --cf command-rwlock-protection.cf command-rwlock-protection.c
require_match "command rwlock unlocked read" \
    "protected member 'value' read without read-holding 'lock'.*\\[unprotected-access\\]" \
    command-rwlock-protection.out
require_match "command rwlock unlocked write" \
    "protected member 'value' modified without write-holding 'lock'.*\\[unprotected-access\\]" \
    command-rwlock-protection.out
require_match "command rwlock reader write" \
    "required lock 'lock' is read-held; write-holding is required" \
    command-rwlock-protection.out
require_match "command rwlock unlocked global" \
    "protected member 'command_rwlock_global_value' read without read-holding 'command_rwlock_global_lock'.*\\[unprotected-access\\]" \
    command-rwlock-protection.out
if [ "$(grep -F -c '[unprotected-access]' \
    command-rwlock-protection.out)" -ne 4 ]; then
	fail "command rwlock protection behavior: expected four findings"
fi

run_failure "command rwlock source conflict" \
    command-rwlock-conflict-source.out "$LOCKLINT" \
    --cf command-rwlock-conflict-source.cf command-rwlock-protection.c
require_match "command rwlock source conflict" \
    "conflicting rwlock protector for data name 'command_rwlock_state::duplicate'" \
    command-rwlock-conflict-source.out
require_match "command rwlock source conflict origin" \
    "previous declaration at command-rwlock-protection.c:44:1" \
    command-rwlock-conflict-source.out

run_failure "command rwlock mutex conflict" \
    command-rwlock-conflict-mutex.out "$LOCKLINT" \
    --cf command-rwlock-conflict-mutex.cf command-rwlock-protection.c
require_match "command rwlock mutex conflict" \
    "conflicting rwlock protector for data name 'command_rwlock_state::mutex_value'" \
    command-rwlock-conflict-mutex.out
require_match "command rwlock mutex conflict origin" \
    "previous declaration at command-rwlock-protection.c:46:1" \
    command-rwlock-conflict-mutex.out

run_failure "command rwlock command conflict" \
    command-rwlock-conflict-command.out "$LOCKLINT" \
    --cf command-rwlock-conflict-command.cf command-rwlock-protection.c
require_match "command rwlock command conflict" \
    "conflicting rwlock protector for data name 'command_rwlock_state::value'" \
    command-rwlock-conflict-command.out
require_match "command rwlock command conflict origin" \
    "previous declaration at command-rwlock-conflict-command.cf:1" \
    command-rwlock-conflict-command.out

run_failure "command rwlock protection arity" \
    command-rwlock-arity.out "$LOCKLINT" \
    --cf command-rwlock-arity.cf command-rwlock-protection.c
require_match "command rwlock protection arity" \
    "declare rwlock-protects-data requires one lock and at least one data name" \
    command-rwlock-arity.out

#
# Verify command-declared entry assertions using formal names exactly as
# written in each function definition.
#
run_capture "command entry assertion behavior" \
    command-entry-assert-native.out "$LOCKLINT" \
    --cf command-entry-assert-native.cf command-entry-assert-native.c
require_match "command mutex entry assertion" \
    "call to 'command_assert_mutex' does not satisfy asserted mutex-held requirement for lock 'mutex'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command conditional mutex entry assertion" \
    "asserted mutex-held requirement for lock 'mutex' is not established on every path calling 'command_assert_mutex'.*\\[conditional-asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command second function entry assertion" \
    "call to 'command_assert_mutex_second' does not satisfy asserted mutex-held requirement for lock 'mutex'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command direct formal entry assertion" \
    "call to 'command_assert_direct' does not satisfy asserted mutex-held requirement for lock 'lock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command nested formal entry assertion" \
    "call to 'command_assert_nested' does not satisfy asserted mutex-held requirement for lock 'nested.lock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command global entry assertion" \
    "call to 'command_assert_global' does not satisfy asserted mutex-held requirement for lock 'command_assert_global_lock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command exact reader rejects writer" \
    "call to 'command_assert_read' does not satisfy asserted read-held requirement for lock 'rwlock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command exact writer rejects reader" \
    "call to 'command_assert_write' does not satisfy asserted write-held requirement for lock 'rwlock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command rw held rejects unheld" \
    "call to 'command_assert_rw' does not satisfy asserted lock-held requirement for lock 'rwlock'.*\\[asserted-lock-requirement\\]" \
    command-entry-assert-native.out
require_match "command assertion provenance" \
    "command-entry-assert-native.cf:[1-9][0-9]*: asserted requirement is here" \
    command-entry-assert-native.out
if [ "$(grep -F -c '[asserted-lock-requirement]' \
    command-entry-assert-native.out)" -ne 10 ]; then
	fail "command entry assertion behavior: expected ten definite findings"
fi
if [ "$(grep -F -c '[conditional-asserted-lock-requirement]' \
    command-entry-assert-native.out)" -ne 1 ]; then
	fail "command entry assertion behavior: expected one conditional finding"
fi
reject_match "command synthetic root release" \
    "command_assert_synthetic_root.*lock-not-held" \
    command-entry-assert-native.out

run_failure "command entry assertion arity" \
    command-entry-assert-arity.out "$LOCKLINT" \
    --cf command-entry-assert-arity.cf command-entry-assert-native.c
require_match "command entry assertion arity" \
    "assert mutex-held requires one lock and at least one function name" \
    command-entry-assert-arity.out

run_failure "command entry assertion mode" \
    command-entry-assert-mode.out "$LOCKLINT" \
    --cf command-entry-assert-mode.cf command-entry-assert-native.c
require_match "command entry assertion mode" \
    "unknown assert subcommand 'bad-mode'" command-entry-assert-mode.out

run_failure "command entry assertion function" \
    command-entry-assert-function.out "$LOCKLINT" \
    --cf command-entry-assert-function.cf command-entry-assert-native.c
require_match "command entry assertion function" \
    "unresolved function name 'missing_command_assert_function'" \
    command-entry-assert-function.out

run_failure "command entry assertion formal" \
    command-entry-assert-formal.out "$LOCKLINT" \
    --cf command-entry-assert-formal.cf command-entry-assert-native.c
require_match "command entry assertion formal" \
    "function 'command_assert_mutex' has no formal named 'missing'" \
    command-entry-assert-formal.out

run_failure "command entry assertion member" \
    command-entry-assert-member.out "$LOCKLINT" \
    --cf command-entry-assert-member.cf command-entry-assert-native.c
require_match "command entry assertion member" \
    "unresolved lock path 'state.missing' in function 'command_assert_mutex'" \
    command-entry-assert-member.out

run_failure "command entry assertion type" \
    command-entry-assert-type.out "$LOCKLINT" \
    --cf command-entry-assert-type.cf command-entry-assert-native.c
require_match "command entry assertion type" \
    "lock path 'state.mutex' is not a readers-writer lock in function 'command_assert_mutex'" \
    command-entry-assert-type.out

#
# Verify command-file readable policy after all translation units have been
# parsed, including a type declared separately in each translation unit.
#
run_capture "command readable provenance" command-readable-annotations.out \
    "$LOCKLINT" --dump-annotations --dump-statistics \
    --cf readable.cf \
    readable.c readable-other.c
require_match "command readable type provenance" \
    "readable.cf:2: DATA_READABLE_WITHOUT_LOCK command_state::readable" \
    command-readable-annotations.out
require_match "command readable object provenance" \
    "readable.cf:4: DATA_READABLE_WITHOUT_LOCK command_global" \
    command-readable-annotations.out
if [ "$(grep -c \
    'MUTEX_PROTECTS_DATA command_state::lock -> command_state::readable' \
    command-readable-annotations.out)" -ne 3 ]; then
	fail "command readable provenance: expected three canonical policy references"
fi
require_match "command source type policy references resolved" \
    '^statistics source_type_policy_refs_resolved 7$' \
    command-readable-annotations.out
require_match "command source type policy references retained" \
    '^statistics source_type_policy_refs_retained 4$' \
    command-readable-annotations.out
require_match "command source type policy references deduplicated" \
    '^statistics source_type_policy_refs_deduplicated 3$' \
    command-readable-annotations.out

run_capture "command type dump" command-types.out \
    "$LOCKLINT" --dump-types --dump-statistics \
    readable.c readable-other.c
if [ "$(grep -c '^type command_state kind=struct ' command-types.out)" \
    -ne 1 ]; then
	fail "command type dump: expected one command_state locklint type"
fi
if [ "$(grep -c '^type command_state_t kind=struct ' command-types.out)" \
    -ne 1 ]; then
	fail "command type dump: expected one command_state_t locklint type"
fi
if [ "$(grep -c '^type duplicate_command_type kind=struct ' \
    command-types.out)" -ne 2 ]; then
	fail "command type dump: expected two distinct locklint types"
fi
require_match "command type dump summary" '^types [1-9][0-9]*$' \
    command-types.out
require_match "command type registry size" \
    '^statistics type_registry_insertions 7$' command-types.out
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
    "$LOCKLINT" --cf readable-arity.cf \
    readable.c
require_match "command readable arity" \
    "declare readable requires one data name" command-readable-arity.out

run_failure "command readable unresolved name" \
    command-readable-unresolved.out "$LOCKLINT" \
    --cf readable-unresolved.cf readable.c
require_match "command readable unresolved name" \
    "unresolved data name 'missing_command_object'" \
    command-readable-unresolved.out

run_failure "command object is not a type" \
    command-readable-object-as-type.out "$LOCKLINT" \
    --cf readable-object-as-type.cf \
    readable.c
require_match "command object is not a type" \
    "unresolved data name 'command_object::readable'" \
    command-readable-object-as-type.out

run_failure "command enum is not an object" command-readable-enum.out \
    "$LOCKLINT" --cf readable-enum.cf \
    readable.c
require_match "command enum is not an object" \
    "unresolved data name 'COMMAND_READABLE_ENUM'" \
    command-readable-enum.out

run_capture "command readable identical types" \
    command-readable-ambiguous.out "$LOCKLINT" \
    --dump-annotations --cf readable-ambiguous.cf \
    readable.c readable-other.c
if [ "$(grep -c \
    'DATA_READABLE_WITHOUT_LOCK duplicate_command_type::value' \
    command-readable-ambiguous.out)" -ne 2 ]; then
	fail "command readable identical types: expected two annotations"
fi

run_failure "command readable inconsistent type" \
    command-readable-inconsistent.out "$LOCKLINT" \
    --cf readable-inconsistent.cf \
    type-name-first.c type-name-second-different.c
require_match "command readable inconsistent type" \
    "inconsistently defined type in data name 'repeated_name::value'" \
    command-readable-inconsistent.out

#
# Verify that explicit entry declarations replace external-linkage-only root
# inference and propagate their competition condition through resolved calls.
# Caller-free functions must remain roots for independent analysis.
#
run_capture "command entry competition" command-entry-competition.out \
    "$LOCKLINT" --dump-callgraph \
    --cf entry-competition.cf \
    entry-competition.c entry-competition-helper.c
require_match "command entry retained caller-free root" \
    "entry-competition.c:47:9: warning: competing threads exist at NO_COMPETING_THREADS assertion" \
    command-entry-competition.out
reject_match "command entry helper assertion" \
    "entry-competition-helper.c:29:9: warning:" \
    command-entry-competition.out
require_match "command entry helper reachable" \
    "^function command_entry_helper .* reachable=yes$" \
    command-entry-competition.out
require_match "command entry declaration provenance" \
    "root declared-entry no-competing-threads entry-competition.cf:2" \
    command-entry-competition.out
if awk '
    /^function command_entry_helper / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root / { found = 1 }
    END { exit found ? 0 : 1 }
' command-entry-competition.out; then
	fail "command entry helper unexpectedly remained an analysis root"
fi
run_failure "command entry arity" command-entry-arity.out \
    "$LOCKLINT" --cf entry-competition-arity.cf \
    entry-competition.c entry-competition-helper.c
require_match "command entry arity" \
    "declare entry requires 'no-competing-threads' and one function name" \
    command-entry-arity.out
run_failure "command entry unresolved" command-entry-unresolved.out \
    "$LOCKLINT" --cf entry-competition-unresolved.cf \
    entry-competition.c entry-competition-helper.c
require_match "command entry unresolved" \
    "unresolved function name 'missing_entry'" \
    command-entry-unresolved.out
run_failure "command entry ambiguous" command-entry-ambiguous.out \
    "$LOCKLINT" --dump-callgraph \
    --cf entry-competition-ambiguous.cf \
    ambiguous-call-first.c ambiguous-call-second.c
require_match "command entry ambiguous" \
    "ambiguous function name 'ambiguous_target'" \
    command-entry-ambiguous.out

#
# Verify analysis-wide automatic root discovery independently of explicit
# entry declarations and per-function external-entry properties.
#
run_capture "default automatic roots" root-discovery-auto.out \
    "$LOCKLINT" --dump-callgraph \
    external-entry.c external-entry-helper.c
reject_match "default known-caller external root" \
    "^  root external-linkage$" root-discovery-auto.out
require_match "default no-caller root" \
    "^  root no-known-direct-caller$" root-discovery-auto.out
require_match "default escaped root" \
    "^  root function-pointer-escape" root-discovery-auto.out

run_capture "explicit automatic roots" root-discovery-auto-explicit.out \
    "$LOCKLINT" --root-discovery=auto --dump-callgraph \
    external-entry.c external-entry-helper.c
compare "default and explicit automatic roots" root-discovery-auto.out \
    root-discovery-auto-explicit.out

run_capture "all exported roots" root-discovery-all-exported.out \
    "$LOCKLINT" --root-discovery=all-exported --dump-callgraph \
    external-entry.c external-entry-helper.c
require_match "all exported external root" \
    "^  root external-linkage$" root-discovery-all-exported.out
require_match "all exported no-caller root" \
    "^  root no-known-direct-caller$" root-discovery-all-exported.out
require_match "all exported escaped root" \
    "^  root function-pointer-escape" root-discovery-all-exported.out
if ! awk '
    /^function external_entry_called / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root external-linkage$/ { found = 1 }
    END { exit found ? 0 : 1 }
' root-discovery-all-exported.out; then
	fail "all exported known-caller function: missing external root"
fi

run_capture "no automatic roots" root-discovery-none.out \
    "$LOCKLINT" --root-discovery=none --dump-callgraph \
    external-entry.c external-entry-helper.c
reject_match "disabled automatic root" \
    "^  root " root-discovery-none.out

run_capture "explicit root without discovery" root-discovery-explicit.out \
    "$LOCKLINT" --root-discovery=none --dump-callgraph \
    --cf root-discovery-explicit.cf \
    external-entry.c external-entry-helper.c
require_match "explicit root without discovery reason" \
    "^  root external-linkage$" root-discovery-explicit.out
require_match "explicit root without discovery provenance" \
    "property external-entry=true root-discovery-explicit.cf:2" \
    root-discovery-explicit.out

run_capture "declared entries without discovery" \
    root-discovery-declared.out \
    "$LOCKLINT" --root-discovery=none --no-check --dump-callgraph \
    --cf entry-competition.cf \
    entry-competition.c entry-competition-helper.c
require_match "declared entry without discovery" \
    "root declared-entry no-competing-threads entry-competition.cf:2" \
    root-discovery-declared.out

run_failure "invalid root discovery" root-discovery-invalid.out \
    "$LOCKLINT" --root-discovery=unknown smoke.c
require_match "invalid root discovery" \
    "unknown root discovery mode 'unknown'" root-discovery-invalid.out

run_failure "missing root discovery" root-discovery-missing.out \
    "$LOCKLINT" --root-discovery smoke.c
require_match "missing root discovery" \
    "root-discovery requires a mode" root-discovery-missing.out

#
# Verify the OSLL-compatible representative-instance policy.  Cross-object
# protection matches the declared lock role with or without the command.
# Exact lock identities remain distinct until the selected controller type is
# merged into one representative instance.
#
run_capture "per-instance identities" merge-instances-before.out \
    "$LOCKLINT" merge-instances.c
reject_match "per-instance cross-object protection" \
    "merge-instances.c:.*protected member 'value' modified without holding 'lock'" \
    merge-instances-before.out
reject_match "per-instance distinct locks" \
    "lock 'lock'.*already held" \
    merge-instances-before.out

run_capture "merged representative instance" merge-instances-after.out \
    "$LOCKLINT" --cf merge-instances.cf \
    merge-instances.c
reject_match "merged cross-object protection" \
    "merge-instances.c:.*protected member 'value'" \
    merge-instances-after.out
require_match "merged member locks" \
    "merge-instances.c:.*warning: lock 'lock' is already held" \
    merge-instances-after.out
reject_match "unrelated instances remain distinct" \
    "lock 'other_lock'.*already held" merge-instances-after.out

run_capture "merged representative audit" merge-instances-types.out \
    "$LOCKLINT" --dump-types --cf merge-instances.cf \
    merge-instances.c
require_match "merged representative audit" \
    "^type merge_instance_controller .* merged-instances=true$" \
    merge-instances-types.out
reject_match "unrelated type audit" \
    "^type merge_instance_unrelated .* merged-instances=true$" \
    merge-instances-types.out

run_failure "merge instances missing type" merge-instances-empty.out \
    "$LOCKLINT" --cf merge-instances-empty.cf \
    merge-instances.c
require_match "merge instances missing type" \
    "merge-instances requires at least one type name" \
    merge-instances-empty.out

run_failure "merge instances invalid type" merge-instances-invalid.out \
    "$LOCKLINT" --cf merge-instances-invalid.cf \
    merge-instances.c
require_match "merge instances invalid type" \
    "invalid type name 'bad-name'" merge-instances-invalid.out

run_failure "merge instances unresolved type" \
    merge-instances-unresolved.out "$LOCKLINT" \
    --cf merge-instances-unresolved.cf \
    merge-instances.c
require_match "merge instances unresolved type" \
    "unresolved type name 'missing_merge_instance_type'" \
    merge-instances-unresolved.out

run_failure "merge instances scalar type" merge-instances-scalar.out \
    "$LOCKLINT" --cf merge-instances-scalar.cf \
    merge-instances.c
require_match "merge instances scalar type" \
    "unresolved type name 'merge_instance_scalar_t'" \
    merge-instances-scalar.out

run_failure "merge instances enum type" merge-instances-enum.out \
    "$LOCKLINT" --cf merge-instances-enum.cf \
    merge-instances.c
require_match "merge instances enum type" \
    "unresolved type name 'merge_instance_enum'" \
    merge-instances-enum.out

run_failure "merge instances inconsistent type" \
    merge-instances-inconsistent.out "$LOCKLINT" \
    --cf merge-instances-inconsistent.cf \
    type-name-first.c type-name-second-different.c
require_match "merge instances inconsistent type" \
    "inconsistently defined type name 'repeated_name'" \
    merge-instances-inconsistent.out

#
# Verify the options-first declaration grammar and the narrow suppression of
# external-linkage automatic roots.  Other independent root reasons remain.
#
run_capture "command external entry" command-external-entry.out \
    "$LOCKLINT" --dump-callgraph \
    --cf external-entry.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry no-caller root" \
    "^function external_entry_caller_free .* reachable=yes$" \
    command-external-entry.out
require_match "command external entry escape root" \
    "^function external_entry_escaped .* reachable=yes$" \
    command-external-entry.out
require_match "command external entry declaration provenance" \
    "property external-entry=false external-entry.cf:1" \
    command-external-entry.out
if awk '
    /^function external_entry_called / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root / { found = 1 }
    END { exit found ? 0 : 1 }
' command-external-entry.out; then
	fail "command external entry known-caller helper remained a root"
fi
if awk '
    /^function external_entry_called_too / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root / { found = 1 }
    END { exit found ? 0 : 1 }
' command-external-entry.out; then
	fail "command external entry second known-caller helper remained a root"
fi
if ! awk '
    /^function external_entry_caller_free / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root no-known-direct-caller$/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-external-entry.out; then
	fail "command external entry lost no-caller root"
fi
if ! awk '
    /^function external_entry_escaped / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-external-entry.out; then
	fail "command external entry lost pointer-escape root"
fi
run_failure "command external entry no name" \
    command-external-entry-no-name.out \
    "$LOCKLINT" --cf external-entry-no-name.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry no name" \
    "declare requires at least one name" \
    command-external-entry-no-name.out
run_failure "command external entry invalid value" \
    command-external-entry-invalid-value.out \
    "$LOCKLINT" --cf external-entry-invalid-value.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry invalid value" \
    "invalid Boolean value 'unknown' for option '--external-entry'" \
    command-external-entry-invalid-value.out
run_failure "command external entry option after name" \
    command-external-entry-option-after-name.out \
    "$LOCKLINT" --cf external-entry-option-after-name.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry option after name" \
    "declaration options must precede names" \
    command-external-entry-option-after-name.out
run_failure "command external entry unresolved" \
    command-external-entry-unresolved.out \
    "$LOCKLINT" --cf external-entry-unresolved.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry unresolved" \
    "unresolved function name 'missing_external_entry'" \
    command-external-entry-unresolved.out
run_failure "command external entry ambiguous" \
    command-external-entry-ambiguous.out \
    "$LOCKLINT" --dump-callgraph \
    --cf external-entry-ambiguous.cf \
    ambiguous-call-first.c ambiguous-call-second.c
require_match "command external entry ambiguous" \
    "ambiguous function name 'ambiguous_target'" \
    command-external-entry-ambiguous.out
run_failure "command external entry conflict" \
    command-external-entry-conflict.out \
    "$LOCKLINT" --dump-callgraph \
    --cf external-entry-conflict.cf \
    external-entry.c external-entry-helper.c
require_match "command external entry conflict" \
    "conflicting value for option '--external-entry' on function 'external_entry_called'" \
    command-external-entry-conflict.out
run_capture "command external entry true" \
    command-external-entry-true.out \
    "$LOCKLINT" --dump-callgraph \
    --cf external-entry-true.cf \
    entry-competition.c entry-competition-helper.c
if ! awk '
    /^function command_entry_helper / { helper = 1; next }
    /^function / { helper = 0 }
    helper && /^  root external-linkage$/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-external-entry-true.out; then
	fail "command external entry true did not retain external root"
fi
require_match "command external entry true provenance" \
    "property external-entry=true external-entry-true.cf:2" \
    command-external-entry-true.out

#
# Verify declared targets for an operation-vector member supplied outside the
# analysis.  A singleton declaration analyzes one target body; a target set
# analyzes both bodies.  A declaration for start must not affect finish.
#
run_capture "command unmodeled indirect call" command-targets-unmodeled.out \
    "$LOCKLINT" --cf targets-unmodeled.cf targets.c
require_match "command unmodeled indirect call" \
    "indirect call through 'command_target_ops::finish' has no target or calling contract \\[unmodeled-indirect-call\\]" \
    command-targets-unmodeled.out
require_match "command unmodeled target advice" \
    "use 'declare targets command_target_ops::finish FUNCTION...' when the target function is known" \
    command-targets-unmodeled.out
require_match "command unmodeled contract advice" \
    "use 'declare contract command_target_ops::finish no-lock-effects' for analysis-specific policy" \
    command-targets-unmodeled.out
require_match "source unmodeled contract advice" \
    "add '_NOTE(DECLARE_CONTRACT(command_target_ops::finish, NO_LOCK_EFFECTS))' for stable interface policy" \
    command-targets-unmodeled.out
run_capture "no-check unmodeled indirect call" \
    command-targets-unmodeled-no-check.out \
    "$LOCKLINT" --no-check --dump-contexts \
    --cf targets-unmodeled.cf targets.c
reject_match "no-check unmodeled indirect call" \
    "unmodeled-indirect-call" command-targets-unmodeled-no-check.out

run_capture "nested unmodeled indirect call" nested-target-unmodeled.out \
    "$LOCKLINT" nested-target.c
require_match "nested unmodeled indirect owner" \
    "indirect call through 'nested_target_ops::finish' has no target or calling contract \\[unmodeled-indirect-call\\]" \
    nested-target-unmodeled.out
reject_match "nested unmodeled outer owner" \
    "nested_target_outer::finish" nested-target-unmodeled.out
require_match "nested unmodeled target advice" \
    "declare targets nested_target_ops::finish FUNCTION..." \
    nested-target-unmodeled.out
require_match "nested unmodeled contract advice" \
    "declare contract nested_target_ops::finish no-lock-effects" \
    nested-target-unmodeled.out
require_match "nested unmodeled source advice" \
    "DECLARE_CONTRACT(nested_target_ops::finish, NO_LOCK_EFFECTS)" \
    nested-target-unmodeled.out

run_capture "nested no-lock contract" nested-target-contract.out \
    "$LOCKLINT" --cf nested-target-contract.cf \
    nested-target.c
reject_match "nested no-lock contract warning" \
    "unmodeled-indirect-call" nested-target-contract.out

run_capture "command singleton target" command-targets-first.out \
    "$LOCKLINT" --dump-callgraph \
    --cf targets-first.cf \
    targets.c
require_match "command singleton target effect" \
    "targets.c:.*: warning: condition wait occurs while holding lock 'command_target_state::first' \\[lock-held-during-wait\\]" \
    command-targets-first.out
reject_match "command singleton unrelated target" \
    "command_target_state::second" command-targets-first.out
require_match "command singleton resolved member call" \
    "resolved-indirect command_target_first tu=targets.c" \
    command-targets-first.out
require_match "command singleton unresolved other member" \
    "call targets.c:.* indirect$" command-targets-first.out
if [ "$(grep -c 'warning:' command-targets-first.out)" -ne 1 ]; then
	fail "command singleton target: expected exactly one warning"
fi

run_capture "command multiple targets" command-targets-both.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_BOTH=1 \
    --dump-callgraph \
    --cf targets-both.cf \
    targets.c
require_match "command first possible target effect" \
    "targets.c:.*: warning: condition wait occurs while holding lock 'command_target_state::first' \\[lock-held-during-wait\\]" \
    command-targets-both.out
require_match "command second possible target effect" \
    "targets.c:.*: warning: condition wait occurs while holding lock 'command_target_state::second' \\[lock-held-during-wait\\]" \
    command-targets-both.out
require_match "command first possible target provenance" \
    "lock is held on a path through call to 'command_target_first'" \
    command-targets-both.out
require_match "command second possible target provenance" \
    "lock is held on a path through call to 'command_target_second'" \
    command-targets-both.out
require_match "command additive target set" \
    "resolved-indirect-targets command_target_first@targets.c command_target_second@targets.c" \
    command-targets-both.out
if [ "$(grep -c 'warning:' command-targets-both.out)" -ne 2 ]; then
	fail "command multiple targets: expected exactly two warnings"
fi

#
# Explicitly dereferencing a function-pointer member is equivalent to calling
# the member directly and must preserve its declared target selector.
#
run_capture "explicitly dereferenced command targets" \
    command-targets-explicit-dereference.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_BOTH=1 \
    -DCOMMAND_TARGETS_EXPLICIT_DEREFERENCE=1 \
    --dump-callgraph --cf targets-both.cf targets.c
require_match "explicit dereference target set" \
    "resolved-indirect-targets command_target_first@targets.c command_target_second@targets.c" \
    command-targets-explicit-dereference.out
require_match "explicit dereference first target effect" \
    "condition wait occurs while holding lock 'command_target_state::first' \\[lock-held-during-wait\\]" \
    command-targets-explicit-dereference.out
require_match "explicit dereference second target effect" \
    "condition wait occurs while holding lock 'command_target_state::second' \\[lock-held-during-wait\\]" \
    command-targets-explicit-dereference.out
if [ "$(grep -c 'warning:' command-targets-explicit-dereference.out)" -ne 2 ];
then
	fail "explicitly dereferenced command targets: expected two warnings"
fi

#
# An exact assignment to a member with a complete target declaration is
# accounted for by that declaration.  An unrelated escape of the same
# function must still retain the unknown-caller root.
#
run_capture "declared member assignment escape" \
    command-targets-declared-assignment.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_ASSIGN_DECLARED=1 --dump-callgraph \
    --cf targets-first.cf targets.c
if awk '
    /^function command_target_first / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-declared-assignment.out; then
	fail "declared member assignment retained pointer-escape root"
fi
require_match "declared member assignment accounting" \
    "^  escape uses exact=1 member-assignments=1 comparisons=0 accounted=1$" \
    command-targets-declared-assignment.out

run_capture "declared member unrelated escape" \
    command-targets-unrelated-assignment.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_ASSIGN_DECLARED=1 \
    -DCOMMAND_TARGETS_ASSIGN_UNRELATED=1 --dump-callgraph \
    --cf targets-first.cf targets.c
if ! awk '
    /^function command_target_first / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-unrelated-assignment.out; then
	fail "unrelated assignment lost pointer-escape root"
fi
require_match "unrelated assignment accounting" \
    "^  escape uses exact=2 member-assignments=1 comparisons=0 accounted=1$" \
    command-targets-unrelated-assignment.out

run_capture "declared member copied escape" \
    command-targets-copied-assignment.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_ASSIGN_DECLARED=1 \
    -DCOMMAND_TARGETS_COPY_DECLARED=1 --dump-callgraph \
    --cf targets-first.cf targets.c
if ! awk '
    /^function command_target_first / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-copied-assignment.out; then
	fail "copied declared member lost pointer-escape root"
fi

run_capture "declared member target comparison" \
    command-targets-target-comparison.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_COMPARE_TARGET=1 --dump-callgraph \
    --cf targets-first.cf targets.c
if awk '
    /^function command_target_first / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-target-comparison.out; then
	fail "function address comparison retained pointer-escape root"
fi
require_match "function address comparison accounting" \
    "^  escape uses exact=1 member-assignments=0 comparisons=1 accounted=1$" \
    command-targets-target-comparison.out

run_capture "multiple declared member assignments" \
    command-targets-declared-assignments.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_BOTH=1 \
    -DCOMMAND_TARGETS_ASSIGN_DECLARED_SECOND=1 --dump-callgraph \
    --cf targets-both.cf targets.c
if awk '
    /^function command_target_(first|second) / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-declared-assignments.out; then
	fail "multiple declared member assignments retained pointer-escape root"
fi
if [ "$(grep -c '^  escape uses exact=1 member-assignments=1 comparisons=0 accounted=1$' \
    command-targets-declared-assignments.out)" -ne 2 ]; then
	fail "multiple declared member assignments were not all accounted"
fi

run_capture "undeclared member assignment" \
    command-targets-undeclared-member.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_ASSIGN_UNDECLARED_MEMBER=1 \
    --dump-callgraph --cf targets-first.cf targets.c
if ! awk '
    /^function command_target_first / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-undeclared-member.out; then
	fail "undeclared member assignment lost pointer-escape root"
fi

run_capture "undeclared target assignment" \
    command-targets-undeclared-target.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_ASSIGN_UNDECLARED_TARGET=1 \
    --dump-callgraph --cf targets-first.cf targets.c
if ! awk '
    /^function command_target_second / { target = 1; next }
    /^function / { target = 0 }
    target && /^  root function-pointer-escape/ { found = 1 }
    END { exit found ? 0 : 1 }
' command-targets-undeclared-target.out; then
	fail "undeclared target assignment lost pointer-escape root"
fi

run_capture "command target return effects" command-targets-effects.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_DIFFERENT_EFFECTS=1 \
    --cf targets-effects.cf targets.c
require_match "first target conditional return effect" \
    "warning: lock 'first' may not be held \\[lock-maybe-not-held\\]" \
    command-targets-effects.out
require_match "second target conditional return effect" \
    "warning: lock 'second' may not be held \\[lock-maybe-not-held\\]" \
    command-targets-effects.out
require_match "first target returned effect" \
    "warning: condition wait may occur while holding lock 'command_target_state::first' \\[lock-maybe-held-during-wait\\]" \
    command-targets-effects.out
require_match "second target returned effect" \
    "warning: condition wait may occur while holding lock 'command_target_state::second' \\[lock-maybe-held-during-wait\\]" \
    command-targets-effects.out
require_match "first target contract mismatch" \
    "function 'command_target_first' has lock acquisitions inconsistent with contract for 'command_target_ops::start' \\[function-contract-mismatch\\]" \
    command-targets-effects.out
require_match "second target contract mismatch" \
    "function 'command_target_second' has lock acquisitions inconsistent with contract for 'command_target_ops::start' \\[function-contract-mismatch\\]" \
    command-targets-effects.out
if [ "$(grep -c 'warning:' command-targets-effects.out)" -ne 6 ]; then
	fail "command target return effects: expected exactly six warnings"
fi

run_capture "source no-lock contract" command-targets-source-contract.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_SOURCE_CONTRACT=1 \
    --dump-annotations --cf targets-unmodeled.cf targets.c
reject_match "source no-lock contract warning" \
    "unmodeled-indirect-call" command-targets-source-contract.out
require_match "source no-lock contract annotation" \
    "DECLARE_CONTRACT command_target_ops::finish NO_LOCK_EFFECTS" \
    command-targets-source-contract.out

run_capture "source representative contract" \
    command-targets-source-representative.out \
    "$LOCKLINT" -DCOMMAND_TARGETS_SOURCE_REPRESENTATIVE=1 \
    --dump-annotations --dump-callgraph \
    --cf targets-unmodeled.cf \
    targets.c representative-functions.c
require_match "source representative contract annotation" \
    "DECLARE_CONTRACT command_target_ops::start command_target_representative" \
    command-targets-source-representative.out
require_match "source representative contract declaration" \
    "contract command_target_ops::start representative command_target_representative@representative-functions.c" \
    command-targets-source-representative.out

run_failure "command contract arity" command-contract-arity.out \
    "$LOCKLINT" --cf targets-contract-arity.cf targets.c
require_match "command contract arity" \
    "declare contract requires one member and one contract" \
    command-contract-arity.out
run_failure "command contract kind" command-contract-kind.out \
    "$LOCKLINT" --cf targets-contract-kind.cf targets.c
require_match "command contract kind" \
    "unresolved function name 'lock-free'" command-contract-kind.out
run_failure "command contract unresolved" command-contract-unresolved.out \
    "$LOCKLINT" --cf targets-contract-unresolved.cf \
    targets.c
require_match "command contract unresolved" \
    "unresolved function-pointer member 'missing_target_ops::finish'" \
    command-contract-unresolved.out
run_failure "command contract non-function" \
    command-contract-non-function.out \
    "$LOCKLINT" --cf targets-contract-non-function.cf \
    targets.c
require_match "command contract non-function" \
    "member 'command_target_state::first' is not a function pointer" \
    command-contract-non-function.out
run_capture "command target and contract" command-contract-conflict.out \
    "$LOCKLINT" --cf targets-contract-conflict.cf \
    targets.c
reject_match "command target and contract conflict" \
    "conflict" \
    command-contract-conflict.out
run_capture "command contract and target" \
    command-contract-reverse-conflict.out \
    "$LOCKLINT" --cf targets-contract-reverse-conflict.cf \
    targets.c
reject_match "command contract and target conflict" \
    "conflict" \
    command-contract-reverse-conflict.out

run_capture "command representative contract" \
    command-contract-representative.out \
    "$LOCKLINT" --dump-callgraph \
    --cf targets-representative.cf \
    targets.c representative-functions.c
require_match "command representative contract declaration" \
    "contract command_target_ops::start representative command_target_representative@representative-functions.c" \
    command-contract-representative.out
require_match "representative contract keeps concrete target" \
    "resolved-indirect command_target_first tu=targets.c" \
    command-contract-representative.out

run_failure "command representative unresolved" \
    command-contract-representative-unresolved.out \
    "$LOCKLINT" \
    --cf targets-contract-representative-unresolved.cf \
    targets.c representative-functions.c
require_match "command representative unresolved" \
    "unresolved function name 'missing_representative'" \
    command-contract-representative-unresolved.out

run_failure "command representative incompatible" \
    command-contract-representative-incompatible.out \
    "$LOCKLINT" \
    --cf targets-contract-representative-incompatible.cf \
    targets.c representative-functions.c
require_match "command representative incompatible" \
    "function 'command_target_incompatible' has incompatible type for member 'command_target_ops::start'" \
    command-contract-representative-incompatible.out

run_capture "representative call behavior" representative-call.out \
    "$LOCKLINT" --dump-callgraph \
    --cf representative-call.cf \
    representative-call.c \
    representative-call-models.c
reject_match "representative call behavior" 'warning:' \
    representative-call.out
require_match "representative call remains indirect" \
    "call representative-call.c:.* indirect$" \
    representative-call.out
reject_match "representative is not a concrete target" \
    "resolved-indirect representative_enter" representative-call.out

run_capture "declared returned object mismatch" \
    declared-return-effect.out \
    "$LOCKLINT" declared-return-effect.c
require_match "declared returned object mismatch" \
    "function 'return_different_object' does not establish declared mutex acquisition of lock 'lock' \\[declared-lock-effect\\]" \
    declared-return-effect.out

run_capture "concrete target precedes representative" \
    representative-call-target.out \
    "$LOCKLINT" \
    --cf representative-call-target.cf \
    representative-call.c \
    representative-call-models.c
require_match "concrete target does not acquire representative lock" \
    "warning: lock 'lock' is not held \\[lock-not-held\\]" \
    representative-call-target.out
reject_match "concrete target call is modeled" \
    "unmodeled-indirect-call" representative-call-target.out
require_match "concrete target differs from representative contract" \
    "function 'concrete_enter' has lock acquisitions inconsistent with contract for 'representative_ops::enter' \\[function-contract-mismatch\\]" \
    representative-call-target.out
if [ "$(grep -c 'warning:' representative-call-target.out)" -ne 2 ]; then
	fail "concrete target precedes representative: expected two warnings"
fi

run_capture "implicit contract consistency" \
    contract-consistency-implicit.out \
    "$LOCKLINT" -DCONTRACT_CONSISTENCY_IMPLICIT=1 \
    contract-consistency.c
require_match "implicit contract mismatch" \
    "function 'consistency_acquire' has lock acquisitions inconsistent with contract for 'implicit_consistency_ops::enter' \\[function-contract-mismatch\\]" \
    contract-consistency-implicit.out
require_match "implicit returned-object contract mismatch" \
    "function 'consistency_create' has lock acquisitions inconsistent with contract for 'implicit_return_consistency_ops::create' \\[function-contract-mismatch\\]" \
    contract-consistency-implicit.out

run_capture "explicit contract consistency" \
    contract-consistency-explicit.out \
    "$LOCKLINT" -DCONTRACT_CONSISTENCY_EXPLICIT=1 \
    --cf contract-consistency-explicit.cf \
    contract-consistency.c
require_match "explicit contract mismatch" \
    "function 'consistency_acquire' has lock acquisitions inconsistent with contract for 'explicit_consistency_ops::enter' \\[function-contract-mismatch\\]" \
    contract-consistency-explicit.out

run_capture "matching representative consistency" \
    contract-consistency-matching.out \
    "$LOCKLINT" -DCONTRACT_CONSISTENCY_MATCHING=1 \
    --cf contract-consistency-matching.cf \
    contract-consistency.c \
    contract-consistency-models.c
reject_match "matching representative consistency" \
    "function-contract-mismatch" contract-consistency-matching.out

run_capture "conflicting representative consistency" \
    contract-consistency-conflicting.out \
    "$LOCKLINT" -DCONTRACT_CONSISTENCY_CONFLICTING=1 \
    --cf contract-consistency-conflicting.cf \
    contract-consistency.c \
    contract-consistency-models.c
require_match "representative contract mismatch" \
    "function 'consistency_none' has lock acquisitions inconsistent with contract for 'conflicting_consistency_ops::enter' \\[function-contract-mismatch\\]" \
    contract-consistency-conflicting.out

run_capture "declared target contract consistency" \
    contract-consistency-command.out \
    "$LOCKLINT" \
    --cf contract-consistency-command.cf \
    contract-consistency.c
require_match "declared target contract mismatch" \
    "function 'consistency_acquire' has lock acquisitions inconsistent with contract for 'command_consistency_ops::enter' \\[function-contract-mismatch\\]" \
    contract-consistency-command.out

run_capture "command equivalent type targets" \
    command-targets-equivalent.out \
    "$LOCKLINT" --dump-callgraph \
    --cf targets-equivalent.cf \
    targets-equivalent-a.c targets-equivalent-b.c
require_match "command equivalent type resolved member call" \
    "resolved-indirect command_equivalent_target tu=targets-equivalent-a.c" \
    command-targets-equivalent.out
reject_match "command equivalent type unresolved member call" \
    "call targets-equivalent-[ab].c:.* indirect$" \
    command-targets-equivalent.out
if [ "$(grep -c 'resolved-indirect command_equivalent_target' \
    command-targets-equivalent.out)" -ne 2 ]; then
	fail "command equivalent type targets: expected two resolved calls"
fi

run_failure "command inconsistent target type" \
    command-targets-inconsistent.out \
    "$LOCKLINT" -DCOMMAND_EQUIVALENT_INCONSISTENT \
    --cf targets-equivalent.cf \
    targets-equivalent-a.c targets-equivalent-b.c
require_match "command inconsistent target type" \
    "inconsistently defined type in function-pointer member 'command_equivalent_ops::start'" \
    command-targets-inconsistent.out

run_failure "command targets invalid member" \
    command-targets-invalid-member.out \
    "$LOCKLINT" --cf targets-invalid-member.cf targets.c
require_match "command targets invalid member" \
    "invalid function-pointer member name 'command_target_ops.start'" \
    command-targets-invalid-member.out
run_failure "command targets ambiguous function" \
    command-targets-ambiguous-function.out \
    "$LOCKLINT" --dump-callgraph \
    --cf targets-ambiguous-function.cf \
    targets-equivalent-b.c \
    ambiguous-call-first.c ambiguous-call-second.c
require_match "command targets ambiguous function" \
    "ambiguous function name 'ambiguous_target'" \
    command-targets-ambiguous-function.out

run_failure "command targets arity" command-targets-arity.out \
    "$LOCKLINT" --cf targets-arity.cf targets.c
require_match "command targets arity" \
    "declare targets requires one member and at least one function name" \
    command-targets-arity.out
run_failure "command targets unresolved member" \
    command-targets-unresolved-member.out \
    "$LOCKLINT" --cf targets-unresolved-member.cf \
    targets.c
require_match "command targets unresolved member" \
    "unresolved function-pointer member 'missing_target_ops::start'" \
    command-targets-unresolved-member.out
run_failure "command targets non-function member" \
    command-targets-non-function-member.out \
    "$LOCKLINT" --cf targets-non-function-member.cf \
    targets.c
require_match "command targets non-function member" \
    "member 'command_target_state::first' is not a function pointer" \
    command-targets-non-function-member.out
run_failure "command targets unresolved function" \
    command-targets-unresolved-function.out \
    "$LOCKLINT" --cf targets-unresolved-function.cf \
    targets.c
require_match "command targets unresolved function" \
    "unresolved function name 'missing_command_target'" \
    command-targets-unresolved-function.out
run_failure "command targets incompatible function" \
    command-targets-incompatible-function.out \
    "$LOCKLINT" --dump-callgraph \
    --cf targets-incompatible-function.cf \
    targets.c
require_match "command targets incompatible function" \
    "function 'command_target_incompatible' has incompatible type for member 'command_target_ops::start'" \
    command-targets-incompatible-function.out

run_capture "rwlock annotations" rwlock-annotations.out \
    "$LOCKLINT" --no-check --dump-annotations rwlock.c
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
    '^point-states created 108 reused 4$' competition-depth-contexts.out
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
    "visibility.c:151:9: warning: visibility annotation has no object \\[visibility-no-object\\]" \
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
    "$LOCKLINT" visibility.c
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
    "$LOCKLINT" visibility-calls.c
compare "call visibility diagnostics" visibility-calls.ref \
    visibility-calls.out

#
# Verify formal, global, and nested visibility effects retain canonical
# identities across translation units independent of input order.
#
run_capture "cross translation unit visibility diagnostics" \
    visibility-cross.out "$LOCKLINT" \
    visibility-cross-caller.c visibility-cross-callee.c
compare "cross translation unit visibility diagnostics" \
    visibility-cross.ref visibility-cross.out

run_capture "reversed cross translation unit visibility diagnostics" \
    visibility-cross-reversed.out "$LOCKLINT" \
    visibility-cross-callee.c visibility-cross-caller.c
compare "reversed cross translation unit visibility diagnostics" \
    visibility-cross.ref visibility-cross-reversed.out

#
# Verify declared competition side effects against every exact return state.
#
run_capture "declared competition effect diagnostics" \
    competition-contracts.out "$LOCKLINT" \
    competition-contracts.c
compare "declared competition effect diagnostics" \
    competition-contracts.ref competition-contracts.out

#
# Verify declared mutex, reader, and writer acquisitions and generic releases
# against every exact exit from distinct synthetic contract contexts.
#
run_capture "declared lock effect diagnostics" declared-effects.out \
    "$LOCKLINT" declared-effects.c
compare "declared lock effect diagnostics" declared-effects.ref \
    declared-effects.out

run_capture "declared release effect diagnostics" \
    declared-releases.out "$LOCKLINT" declared-releases.c
compare "declared release effect diagnostics" \
    declared-releases.ref declared-releases.out

run_capture "declared upgrade effect diagnostics" \
    rwlock-transition-effects-1.out "$LOCKLINT" \
    -DRWLOCK_TRANSITION_EFFECT_VARIANT=1 rwlock-transition-effects.c
compare "declared upgrade effect diagnostics" \
    rwlock-transition-effects-1.ref rwlock-transition-effects-1.out

run_capture "declared downgrade effect diagnostics" \
    rwlock-transition-effects-2.out "$LOCKLINT" \
    -DRWLOCK_TRANSITION_EFFECT_VARIANT=2 rwlock-transition-effects.c
compare "declared downgrade effect diagnostics" \
    rwlock-transition-effects-2.ref rwlock-transition-effects-2.out

#
# Verify canonical absolute lock identities retain declared validation and
# exact acquired/released state through direct calls and formal wrappers.
#
run_capture "absolute declared effect diagnostics" \
    declared-effects-absolute.out "$LOCKLINT" \
    declared-effects-absolute.c
compare "absolute declared effect diagnostics" \
    declared-effects-absolute.ref declared-effects-absolute.out

run_capture "lock effect diagnostics" effects.out \
    "$LOCKLINT" effects.c
compare "lock effect diagnostics" effects.ref effects.out

run_capture "user rwlock core state" rwlock-core-user.out \
    "$LOCKLINT" -DLOCKLINT_RWLOCK_CORE_ONLY rwlock.c
compare "user rwlock core state" rwlock-core.ref rwlock-core-user.out

run_capture "kernel rwlock core state" rwlock-core-kernel.out \
    "$LOCKLINT" -D_KERNEL -DLOCKLINT_RWLOCK_CORE_ONLY rwlock.c
compare "kernel rwlock core state" rwlock-core.ref rwlock-core-kernel.out

run_capture "user rwlock assertions" rwlock-user.out \
    "$LOCKLINT" rwlock.c
compare "user rwlock assertions" rwlock-user.ref rwlock-user.out

run_capture "kernel rwlock assertions" rwlock-kernel.out \
    "$LOCKLINT" -D_KERNEL rwlock.c
compare "kernel rwlock assertions" rwlock-kernel.ref rwlock-kernel.out

run_capture "direct assertion call sites" assertion-requirements.out \
    "$LOCKLINT" assertion-requirements.c
compare "direct assertion call sites" assertion-requirements.ref \
    assertion-requirements.out

run_capture "wrapped assertion call sites" \
    assertion-requirement-wrappers.out \
    "$LOCKLINT" assertion-requirement-wrappers.c
compare "wrapped assertion call sites" \
    assertion-requirement-wrappers.ref \
    assertion-requirement-wrappers.out

run_capture "same-actual assertion aliases" assertion-alias-same.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=1 assertion-alias.c
reject_match "same-actual assertion aliases" "warning:" \
    assertion-alias-same.out

run_capture "distinct assertion aliases" assertion-alias-distinct.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=2 assertion-alias.c
compare "distinct assertion aliases" assertion-alias-distinct.ref \
    assertion-alias-distinct.out

run_capture "opposite assertion aliases" assertion-alias-opposite.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=3 assertion-alias.c
compare "opposite assertion aliases" assertion-alias-opposite.ref \
    assertion-alias-opposite.out

run_capture "same-actual wrapped assertion aliases" \
    assertion-alias-wrapper-same.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=4 assertion-alias.c
reject_match "same-actual wrapped assertion aliases" "warning:" \
    assertion-alias-wrapper-same.out

run_capture "distinct wrapped assertion aliases" \
    assertion-alias-wrapper-distinct.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=5 assertion-alias.c
compare "distinct wrapped assertion aliases" \
    assertion-alias-wrapper-distinct.ref \
    assertion-alias-wrapper-distinct.out

run_capture "internally aliased assertion" assertion-alias-internal.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=6 assertion-alias.c
reject_match "internally aliased assertion" "warning:" \
    assertion-alias-internal.out

run_capture "invalidated assertion alias" assertion-alias-invalidated.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=7 assertion-alias.c
compare "invalidated assertion alias" assertion-alias-invalidated.ref \
    assertion-alias-invalidated.out

run_capture "multiple assertion aliases" assertion-alias-multiple.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=8 assertion-alias.c
compare "multiple assertion aliases" assertion-alias-multiple.ref \
    assertion-alias-multiple.out

run_capture "merged assertion aliases" assertion-alias-merged.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=9 assertion-alias.c
reject_match "merged assertion aliases" "warning:" \
    assertion-alias-merged.out

run_capture "assertion alias overflow replacement" \
    assertion-alias-overflow.out \
    "$LOCKLINT" -DASSERTION_ALIAS_VARIANT=10 assertion-alias.c
compare "assertion alias overflow replacement" \
    assertion-alias-overflow.ref assertion-alias-overflow.out

run_capture "user rwlock call state" rwlock-calls-user.out \
    "$LOCKLINT" rwlock-calls.c
compare "user rwlock call state" rwlock-calls.ref rwlock-calls-user.out

run_capture "kernel rwlock call state" rwlock-calls-kernel.out \
    "$LOCKLINT" -D_KERNEL rwlock-calls.c
compare "kernel rwlock call state" rwlock-calls.ref rwlock-calls-kernel.out

run_capture "rwlock downgrade state" rwlock-downgrade.out \
    "$LOCKLINT" rwlock-downgrade.c
compare "rwlock downgrade state" rwlock-downgrade.ref \
    rwlock-downgrade.out

run_capture "rwlock tryupgrade state" rwlock-tryupgrade.out \
    "$LOCKLINT" rwlock-tryupgrade.c
compare "rwlock tryupgrade state" rwlock-tryupgrade.ref \
    rwlock-tryupgrade.out

run_capture "rwlock tryenter state" rwlock-tryenter.out \
    "$LOCKLINT" rwlock-tryenter.c
compare "rwlock tryenter state" rwlock-tryenter.ref rwlock-tryenter.out

run_capture "mutex tryenter state" mutex-tryenter.out \
    "$LOCKLINT" mutex-tryenter.c
compare "mutex tryenter state" mutex-tryenter.ref mutex-tryenter.out

run_capture "mutex trylock state" mutex-trylock.out \
    "$LOCKLINT" mutex-trylock.c
compare "mutex trylock state" mutex-trylock.ref mutex-trylock.out

run_capture "mutex lock result state" mutex-lock-result.out \
    "$LOCKLINT" mutex-lock-result.c
compare "mutex lock result state" mutex-lock-result.ref \
    mutex-lock-result.out

run_capture "condition wait state and order" condition-wait.out \
    "$LOCKLINT" condition-wait.c
compare "condition wait state and order" condition-wait.ref \
    condition-wait.out

run_capture "condition wait mutex pairing" condition-wait-pairing.out \
    "$LOCKLINT" condition-wait-pairing.c
compare "condition wait mutex pairing" condition-wait-pairing.ref \
    condition-wait-pairing.out

run_capture "other locks held during condition wait" \
    condition-wait-other-lock.out "$LOCKLINT" \
    condition-wait-other-lock.c
require_match "condition wait definite other lock" \
    "condition wait occurs while holding lock 'wait_other_state::other_first' \\[lock-held-during-wait\\]" \
    condition-wait-other-lock.out
require_match "condition wait conditional other lock" \
    "condition wait may occur while holding lock 'wait_other_state::other_first' \\[lock-maybe-held-during-wait\\]" \
    condition-wait-other-lock.out
require_match "condition wait second other lock" \
    "condition wait occurs while holding lock 'wait_other_state::other_second' \\[lock-held-during-wait\\]" \
    condition-wait-other-lock.out
require_match "wrapped condition wait provenance" \
    "lock is held on a path through call to 'wait_other_helper'" \
    condition-wait-other-lock.out
if [ "$(grep -Ec \
    '\[lock-(maybe-)?held-during-wait\]' \
    condition-wait-other-lock.out)" -ne 5 ]; then
	fail "condition wait other locks: expected exactly five warnings"
fi

#
# A definite nested mutex acquisition terminates only that semantic path.
# Direct and formal-callback calls behave the same, including through an
# equivalent local pointer alias.
#
run_capture "held lock through callee" held-lock-callee.out \
    "$LOCKLINT" held-lock-callee.c
require_match "direct callee nested acquisition" \
    "held-lock-callee.c:64:24: warning: call to 'balanced_callee' acquires already-held lock 'lock' \\[lock-already-held\\]" \
    held-lock-callee.out
require_match "formal callback nested acquisition" \
    "held-lock-callee.c:74:24: warning: call to 'invoke_callback' acquires already-held lock 'lock' \\[lock-already-held\\]" \
    held-lock-callee.out
require_match "direct alias nested acquisition" \
    "held-lock-callee.c:105:30: warning: call to 'balanced_alias_callee' acquires already-held lock 'lock' \\[lock-already-held\\]" \
    held-lock-callee.out
require_match "formal callback alias nested acquisition" \
    "held-lock-callee.c:115:30: warning: call to 'invoke_alias_callback' acquires already-held lock 'lock' \\[lock-already-held\\]" \
    held-lock-callee.out
reject_match "held lock through callee path termination" \
    "\\[lock-not-held\\]" \
    held-lock-callee.out
if [ "$(grep -c 'warning:' held-lock-callee.out)" -ne 4 ]; then
	fail "held lock through callee: expected exactly four warnings"
fi

#
# Verify exact callback targets follow function-pointer formals without
# combining the targets or held-lock states of distinct callers.
#
run_capture "formal callback propagation" formal-callback.out \
    "$LOCKLINT" formal-callback.c
require_match "first formal callback target" \
    "formal-callback.c:46:16: warning: condition wait may occur while holding lock 'formal_callback_state::outer_first' \\[lock-maybe-held-during-wait\\]" \
    formal-callback.out
require_match "second formal callback target" \
    "formal-callback.c:54:16: warning: condition wait may occur while holding lock 'formal_callback_state::outer_second' \\[lock-maybe-held-during-wait\\]" \
    formal-callback.out
if [ "$(grep -c 'warning:' formal-callback.out)" -ne 2 ]; then
	fail "formal callback propagation: expected exactly two warnings"
fi

#
# Verify exact callback targets survive stores and loads of a function-pointer
# member without combining the targets or held-lock states of distinct callers.
#
run_capture "stored callback propagation" stored-callback.out \
    "$LOCKLINT" stored-callback.c
require_match "first stored callback target" \
    "stored-callback.c:55:16: warning: condition wait may occur while holding lock 'stored_callback_state::outer_first' \\[lock-maybe-held-during-wait\\]" \
    stored-callback.out
require_match "second stored callback target" \
    "stored-callback.c:63:16: warning: condition wait may occur while holding lock 'stored_callback_state::outer_second' \\[lock-maybe-held-during-wait\\]" \
    stored-callback.out
reject_match "stored callback target retained across call" \
    "stored-callback.c:55:16:.*stored_callback_state::outer_second" \
    stored-callback.out
require_match "invalidated stored callback is unmodeled" \
    "stored-callback.c:99:25: warning: indirect call through 'stored_request::callback' has no target or calling contract \\[unmodeled-indirect-call\\]" \
    stored-callback.out
require_match "cleared stored callback is unmodeled" \
    "stored-callback.c:110:25: warning: indirect call through 'stored_request::callback' has no target or calling contract \\[unmodeled-indirect-call\\]" \
    stored-callback.out
if [ "$(grep -c 'warning:' stored-callback.out)" -ne 4 ]; then
	fail "stored callback propagation: expected exactly four warnings"
fi

run_capture "stored callback helper projection" stored-callback-helper.out \
    "$LOCKLINT" --dump-contexts stored-callback-helper.c
require_match "stored callback helper demand count" \
    "^stored-target-demands 1$" stored-callback-helper.out
require_match "first stored callback helper target" \
    "stored-callback-helper.c:56:16: warning: condition wait may occur while holding lock 'stored_callback_helper_state::outer_first' \\[lock-maybe-held-during-wait\\]" \
    stored-callback-helper.out
require_match "second stored callback helper target" \
    "stored-callback-helper.c:64:16: warning: condition wait may occur while holding lock 'stored_callback_helper_state::outer_second' \\[lock-maybe-held-during-wait\\]" \
    stored-callback-helper.out
reject_match "unpassed second helper target" \
    "stored-callback-helper.c:64:16:.*stored_callback_helper_state::outer_first" \
    stored-callback-helper.out
reject_match "unpassed first helper target" \
    "stored-callback-helper.c:56:16:.*stored_callback_helper_state::outer_second" \
    stored-callback-helper.out
if [ "$(grep -c 'warning:' stored-callback-helper.out)" -ne 2 ]; then
	fail "stored callback helper projection: expected exactly two warnings"
fi

run_capture "forwarded stored callback projection" \
    stored-callback-forward.out "$LOCKLINT" --dump-contexts \
    stored-callback-forward.c
require_match "forwarded stored callback demand count" \
    "^stored-target-demands 2$" stored-callback-forward.out
require_match "first forwarded stored callback target" \
    "stored-callback-forward.c:55:16: warning: condition wait may occur while holding lock 'stored_callback_forward_state::outer_first' \\[lock-maybe-held-during-wait\\]" \
    stored-callback-forward.out
require_match "second forwarded stored callback target" \
    "stored-callback-forward.c:63:16: warning: condition wait may occur while holding lock 'stored_callback_forward_state::outer_second' \\[lock-maybe-held-during-wait\\]" \
    stored-callback-forward.out
reject_match "unpassed second forwarded target" \
    "stored-callback-forward.c:63:16:.*stored_callback_forward_state::outer_first" \
    stored-callback-forward.out
reject_match "unpassed first forwarded target" \
    "stored-callback-forward.c:55:16:.*stored_callback_forward_state::outer_second" \
    stored-callback-forward.out
if [ "$(grep -c 'warning:' stored-callback-forward.out)" -ne 2 ]; then
	fail "forwarded stored callback projection: expected exactly two warnings"
fi

run_capture "operation family profile collection" operation-profiles.out \
    "$LOCKLINT" --dump-contexts operation-profiles.c
require_match "operation family profile count" \
    "^operation-family-profiles 3$" operation-profiles.out
require_match "operation family profile entry count" \
    "^operation-family-profile-entries 6$" operation-profiles.out
require_match "operation family index key count" \
    "^operation-family-index-keys 4$" operation-profiles.out
require_match "operation family index candidate count" \
    "^operation-family-index-candidates 6$" operation-profiles.out
require_match "first profile start target" \
    "operation-profiles.c:163:16: warning: condition wait occurs while holding lock 'operation_state::first' \\[lock-held-during-wait\\]" \
    operation-profiles.out
require_match "first profile start effect at second wait" \
    "operation-profiles.c:167:16: warning: condition wait occurs while holding lock 'operation_state::first' \\[lock-held-during-wait\\]" \
    operation-profiles.out
require_match "first profile finish target" \
    "operation-profiles.c:167:16: warning: condition wait occurs while holding lock 'operation_state::first_done' \\[lock-held-during-wait\\]" \
    operation-profiles.out
require_match "second profile start target" \
    "operation-profiles.c:179:16: warning: condition wait occurs while holding lock 'operation_state::second' \\[lock-held-during-wait\\]" \
    operation-profiles.out
require_match "second profile start effect at second wait" \
    "operation-profiles.c:183:16: warning: condition wait occurs while holding lock 'operation_state::second' \\[lock-held-during-wait\\]" \
    operation-profiles.out
require_match "second profile finish target" \
    "operation-profiles.c:183:16: warning: condition wait occurs while holding lock 'operation_state::second_done' \\[lock-held-during-wait\\]" \
    operation-profiles.out
reject_match "first consumer excludes second profile" \
    "operation-profiles.c:16[37]:16:.*operation_state::second" \
    operation-profiles.out
reject_match "second consumer excludes first profile" \
    "operation-profiles.c:1\\(79\\|83\\):16:.*operation_state::first" \
    operation-profiles.out
require_match "incomplete operation profile is unmodeled" \
    "operation-profiles.c:193:26: warning: indirect call through 'operation_vector::start' has no target or calling contract \\[unmodeled-indirect-call\\]" \
    operation-profiles.out
require_match "first start profile contract mismatch" \
    "function 'first_start' has lock acquisitions inconsistent with contract for 'operation_vector::start' \\[function-contract-mismatch\\]" \
    operation-profiles.out
require_match "first finish profile contract mismatch" \
    "function 'first_finish' has lock acquisitions inconsistent with contract for 'operation_vector::finish' \\[function-contract-mismatch\\]" \
    operation-profiles.out
require_match "second start profile contract mismatch" \
    "function 'second_start' has lock acquisitions inconsistent with contract for 'operation_vector::start' \\[function-contract-mismatch\\]" \
    operation-profiles.out
require_match "second finish profile contract mismatch" \
    "function 'second_finish' has lock acquisitions inconsistent with contract for 'operation_vector::finish' \\[function-contract-mismatch\\]" \
    operation-profiles.out
require_match "unrelated start profile contract mismatch" \
    "function 'first_start' has lock acquisitions inconsistent with contract for 'unrelated_vector::start' \\[function-contract-mismatch\\]" \
    operation-profiles.out
require_match "unrelated finish profile contract mismatch" \
    "function 'first_finish' has lock acquisitions inconsistent with contract for 'unrelated_vector::finish' \\[function-contract-mismatch\\]" \
    operation-profiles.out
if [ "$(grep -c 'warning:' operation-profiles.out)" -ne 13 ]; then
	fail "operation profile provenance: expected exactly thirteen warnings"
fi

run_capture "operation family contract consistency" \
    operation-contracts.out "$LOCKLINT" \
    --cf operation-contracts.cf operation-contracts.c
require_match "operation profile target contract mismatch" \
    "function 'operation_contract_acquire' has lock acquisitions inconsistent with contract for 'operation_contracts::start' \\[function-contract-mismatch\\]" \
    operation-contracts.out
reject_match "balanced nested profile target contract" \
    "function 'operation_contract_balanced'.*\\[function-contract-mismatch\\]" \
    operation-contracts.out
if [ "$(grep -c 'warning:' operation-contracts.out)" -ne 2 ]; then
	fail "operation family contract consistency: expected two warnings"
fi

run_capture "competition protected accesses" competition-accesses.out \
    "$LOCKLINT" -DCOMPETITION_ACCESS_ONLY competition-depth.c
for location in 50 52 54 70 83 115
do
	require_match "definite competing access" \
	    "competition-depth.c:$location:27: warning: protected member 'protected' modified without holding 'lock' \\[unprotected-access\\]" \
	    competition-accesses.out
done
for location in 104 154
do
	require_match "conditional competing access" \
	    "competition-depth.c:$location:27: warning: protection for member 'protected' is not established on every path \\[conditional-protection\\]" \
	    competition-accesses.out
done
if [ "$(grep -Ec '\[(unprotected-access|conditional-protection)\]' \
    competition-accesses.out)" -ne 8 ]; then
	fail "competition protected accesses: expected exactly eight warnings"
fi

require_match "definite unmatched competition decrement" \
    "competition-depth.c:91:9: warning: competition depth decremented below zero \\[competition-underflow\\]" \
    competition-depth-contexts.out
require_match "definite recovered competition decrement" \
    "competition-depth.c:138:9: warning: competition depth decremented below zero \\[competition-underflow\\]" \
    competition-depth-contexts.out
require_match "possible loop competition decrement" \
    "competition-depth.c:151:17: warning: competition depth may be decremented below zero \\[competition-maybe-underflow\\]" \
    competition-depth-contexts.out
if [ "$(grep -Ec '\[competition-(maybe-)?underflow\]' \
    competition-depth-contexts.out)" -ne 3 ]; then
	fail "competition underflow diagnostics: expected exactly three warnings"
fi
for location in 125 127 129
do
	require_match "definite competing read-only write" \
	    "competition-depth.c:$location:27: warning: read-only data 'read_only' modified while visible to competing threads \\[read-only-visible\\]" \
	    competition-depth-contexts.out
done
require_match "possible competing read-only write" \
    "competition-depth.c:161:27: warning: read-only data 'read_only' may be modified while visible to competing threads \\[read-only-maybe-visible\\]" \
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
    "$LOCKLINT" --no-check --dump-callgraph calls.c
compare "calls callgraph" calls-callgraph.ref calls-callgraph.out

run_capture "cross translation unit callgraph" cross-callgraph.out \
    "$LOCKLINT" --no-check --dump-callgraph cross-caller.c cross-callee.c
compare "cross translation unit callgraph" cross-callgraph.ref \
    cross-callgraph.out

run_capture "function pointers callgraph" function-pointers-callgraph.out \
    "$LOCKLINT" --no-check --dump-callgraph function-pointers.c
compare "function pointers callgraph" function-pointers-callgraph.ref \
    function-pointers-callgraph.out

run_capture "unanalyzed callbacks" unanalyzed-callback.out \
    "$LOCKLINT" unanalyzed-callback.c
for function in unanalyzed_ops_only unanalyzed_pointer_only \
    unanalyzed_ops_and_call
do
	require_match "unanalyzed callback $function" \
	    "function '$function' escapes as a callback, but no unique definition is available \\[unanalyzed-callback\\]" \
	    unanalyzed-callback.out
done
reject_match "unanalyzed direct call" \
    "function 'unanalyzed_call_only'.*\\[unanalyzed-callback\\]" \
    unanalyzed-callback.out
reject_match "analyzed callback" \
    "function 'unanalyzed_resolved_callback'.*\\[unanalyzed-callback\\]" \
    unanalyzed-callback.out
if [ "$(grep -c '\[unanalyzed-callback\]' unanalyzed-callback.out)" -ne 3 ];
then
	fail "unanalyzed callbacks: expected exactly three warnings"
fi

run_capture "indirect calls callgraph" indirect-calls-callgraph.out \
    "$LOCKLINT" --no-check --dump-callgraph indirect-calls.c
compare "indirect calls callgraph" indirect-calls-callgraph.ref \
    indirect-calls-callgraph.out

run_capture "ambiguous call callgraph" ambiguous-call-callgraph.out \
    "$LOCKLINT" --parser-warnings --dump-callgraph ambiguous-call-caller.c \
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
    "$LOCKLINT" --no-check --dump-callgraph \
    gnu-extern-inline-no-external.c
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
# Verify that repeated definitions from a shared header are accepted only
# when their layouts agree across translation units.
#
run_capture "shared header type consistency" type-consistency.out \
    "$LOCKLINT" --dump-types type-consistency-first.c \
    type-consistency-second.c
reject_match "shared header type consistency" "inconsistently defined" \
    type-consistency.out
if [ "$(grep -c '^type shared_type kind=struct source=type-consistency.h:' \
    type-consistency.out)" -ne 1 ]; then
	fail "shared header type consistency: expected one locklint type"
fi
require_match "shared header type instance count" \
    '^type shared_type .* instances=2$' type-consistency.out

run_failure "shared header type inconsistency" \
    type-consistency-mismatch.out "$LOCKLINT" --dump-types \
    type-consistency-first.c type-consistency-mismatch.c
require_match "shared header type inconsistency" \
    "struct 'shared_type' is inconsistently defined" \
    type-consistency-mismatch.out

run_capture "same-name identical types" type-name-same.out \
    "$LOCKLINT" --dump-types type-name-first.c type-name-second-same.c
reject_match "same-name identical types" "same name but a different layout" \
    type-name-same.out

run_capture "same-name different types" type-name-different.out \
    "$LOCKLINT" --dump-types type-name-first.c \
    type-name-second-different.c
require_match "same-name different types" \
    "struct 'repeated_name' has the same name but a different layout" \
    type-name-different.out

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
    "LOCK_ROLE_PROTECTS_DATA requires a type-member lock role" \
    annotation-errors.out
require_match "annotation errors" \
    "LOCK_ROLE_PROTECTS_DATA requires type-member data" \
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
run_capture "default lock transition diagnostics" \
    lock-transition-diagnostics.out "$LOCKLINT" check.c
require_match "unprotected read diagnostic" \
    "check.c:49:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "unprotected write diagnostic" \
    "check.c:103:14: warning: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "second unprotected read diagnostic" \
    "check.c:55:30: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    lock-transition-diagnostics.out
require_match "conditional protection diagnostic" \
    "check.c:65:22: warning: protection for member 'value' is not established on every path \\[conditional-protection\\]" \
    lock-transition-diagnostics.out
require_match "mixed release diagnostic" \
    "check.c:66:19: warning: lock 'lock' may not be held \\[lock-maybe-not-held\\]" \
    lock-transition-diagnostics.out

require_match "mixed acquire diagnostic" \
    "check.c:96:20: warning: lock 'lock' may already be held \\[lock-maybe-already-held\\]" \
    lock-transition-diagnostics.out
require_match "conditional held on return" \
    "check.c:88:30: warning: lock 'lock' held on only some paths returning from 'check_side_effect' \\[lock-maybe-held-on-return\\]" \
    lock-transition-diagnostics.out
if [ "$(grep -c 'warning:' lock-transition-diagnostics.out)" -ne 7 ]; then
	fail "lock transition diagnostics: expected exactly seven warnings"
fi

run_capture "no-check context dump" no-check-contexts.out \
    "$LOCKLINT" --no-check --dump-contexts check.c
require_match "no-check context dump" '^roots ' \
    no-check-contexts.out
reject_match "no-check lock diagnostics" 'warning:' \
    no-check-contexts.out

run_capture "no-check protection-state dump" \
    no-check-protection-states.out \
    "$LOCKLINT" --no-check --dump-protection-states data-policy.c
require_match "no-check protection-state dump" \
    '^data-policy.c:.*protection-state ' no-check-protection-states.out
reject_match "no-check protection-state diagnostics" 'warning:' \
    no-check-protection-states.out

#
# Record which source-level read-modify-write accesses survive lowering.
# Isolated expressions retain their read and modification.  Reuse across the
# conditional compound assignment and folding across the sequence do not.
#
run_capture "read-modify-write access diagnostics" rmw-access.out \
    "$LOCKLINT" rmw-access.c
for location in 49 55 61
do
	require_match "isolated read-modify-write read" \
	    "rmw-access.c:$location:.*protected member 'value' read without holding 'lock'" \
	    rmw-access.out
	require_match "isolated read-modify-write modification" \
	    "rmw-access.c:$location:.*protected member 'value' modified without holding 'lock'" \
	    rmw-access.out
done
require_match "reused condition read" \
    "rmw-access.c:67:.*protected member 'value' read without holding 'lock'" \
    rmw-access.out
require_match "reused compound modification" \
    "rmw-access.c:68:.*protected member 'value' modified without holding 'lock'" \
    rmw-access.out
reject_match "reused compound read" \
    "rmw-access.c:68:.*protected member 'value' read without holding 'lock'" \
    rmw-access.out
require_match "plain store modification" \
    "rmw-access.c:74:.*protected member 'value' modified without holding 'lock'" \
    rmw-access.out
reject_match "plain store read" \
    "rmw-access.c:74:.*protected member 'value' read without holding 'lock'" \
    rmw-access.out
require_match "folded sequence read" \
    "rmw-access.c:80:.*protected member 'value' read without holding 'lock'" \
    rmw-access.out
reject_match "folded middle expression access" "rmw-access.c:81:" \
    rmw-access.out
require_match "folded sequence modification" \
    "rmw-access.c:82:.*protected member 'value' modified without holding 'lock'" \
    rmw-access.out
if [ "$(grep -c '\[unprotected-access\]' rmw-access.out)" -ne 11 ]; then
	fail "read-modify-write access diagnostics: expected exactly eleven warnings"
fi

#
# Verify that a cleanup merge which adds NULL to the normal pointer
# alternatives retains the identity of a possibly held lock.
#
run_capture "nullable phi lock identity" nullable-phi-lock.out \
    "$LOCKLINT" --root-discovery=all-exported \
    -O2 -fno-inline-functions \
    nullable-phi-lock.c
require_match "nullable phi lock identity" \
    "nullable-phi-lock.c:66:28: warning: lock 'lock' may already be held \\[lock-maybe-already-held\\]" \
    nullable-phi-lock.out
if [ "$(grep -c 'warning:' nullable-phi-lock.out)" -ne 1 ]; then
	fail "nullable phi lock identity: expected exactly one warning"
fi

#
# Verify structure-valued global and member mutex identities.
#
run_capture "structure-valued mutex diagnostics" struct-lock-diagnostics.out \
    "$LOCKLINT" struct-lock.c
compare "structure-valued mutex diagnostics" struct-lock.ref \
    struct-lock-diagnostics.out

#
# Verify that competition assertions validate but do not alter the state
# reaching subsequent protected accesses.
#
run_capture "competition assertion diagnostics" assertion-diagnostics.out \
    "$LOCKLINT" assertions.c
compare "assertion diagnostics" assertions.ref assertion-diagnostics.out

#
# NO_LOCKS_HELD validates the complete reached lock set without changing it.
# Report one finding at the assertion site, including when caller-held state
# reaches a callee assertion or only some contexts hold a lock.
#
echo "test: no locks held assertions"
: > no-locks-held.out
for case in \
    "empty 1" \
    "released 2" \
    "mutex 3" \
    "reader 4" \
    "writer 5" \
    "conditional 6" \
    "multiple 7" \
    "caller 8"
do
    set -- $case
    echo "case $1" >> no-locks-held.out
    if ! "$LOCKLINT" -DNO_LOCKS_HELD_VARIANT="$2" no-locks-held.c \
        >> no-locks-held.out 2>&1
    then
        fail "no locks held assertions: $1 command failed"
    fi
done
compare "no locks held assertions" no-locks-held.ref no-locks-held.out

#
# Verify mutex, readable-without-lock, and scheme data-policy interaction.
#
run_capture "mutex data policy diagnostics" data-policy-diagnostics.out \
    "$LOCKLINT" data-policy.c
compare "mutex data policy diagnostics" data-policy.ref \
    data-policy-diagnostics.out

#
# C const qualification on the underlying object or member supplies implicit
# read-only policy.  Qualification on an access expression is characterized
# separately and does not define the underlying object's policy.
#
echo "test: implicit const data policy"
: > implicit-const.out
for case in \
    "mutable-control 1" \
    "explicit-control 2" \
    "const-scalar 3" \
    "const-aggregate 4" \
    "forced-write 7"
do
    set -- $case
    echo "case $1" >> implicit-const.out
    if ! "$LOCKLINT" -DIMPLICIT_CONST_VARIANT="$2" implicit-const.c \
        implicit-const-def.c >> implicit-const.out 2>&1
    then
        fail "implicit const data policy: $1 command failed"
    fi
done
compare "implicit const data policy" implicit-const.ref implicit-const.out

run_capture "protection state dump" protection-states.out \
    "$LOCKLINT" --dump-protection-states data-policy.c
require_match "unprotected protection state" \
    "data-policy.c:78:.*protection-state load member='protected' function=check_data_policy states=1 lock=0 invisible=0 no-competition=0 conditional=0 unprotected=1" \
    protection-states.out
require_match "lock-held protection state" \
    "data-policy.c:101:.*protection-state store member='protected' function=check_data_policy states=1 lock=1 invisible=0 no-competition=0 conditional=0 unprotected=0" \
    protection-states.out

#
# A policy rooted in a containing type takes precedence over a policy which
# matches through an embedded aggregate.  Equal-specificity declarations
# retain source-order replacement semantics.
#
run_capture "nested scheme after containing mutex" \
    scheme-mutex-precedence-separate-1.out "$LOCKLINT" \
    scheme-mutex-precedence-access.c scheme-mutex-precedence-scheme.c
require_match "nested scheme after containing mutex" \
    "scheme-mutex-precedence-access.c:26:.*protected member 'inner.value' modified without holding 'lock'" \
    scheme-mutex-precedence-separate-1.out

run_capture "containing mutex after nested scheme" \
    scheme-mutex-precedence-separate-2.out "$LOCKLINT" \
    scheme-mutex-precedence-scheme.c scheme-mutex-precedence-access.c
require_match "containing mutex after nested scheme" \
    "scheme-mutex-precedence-access.c:26:.*protected member 'inner.value' modified without holding 'lock'" \
    scheme-mutex-precedence-separate-2.out

run_capture "same-TU nested scheme and containing mutex" \
    scheme-mutex-precedence-local.out "$LOCKLINT" \
    scheme-mutex-precedence-local.c
require_match "same-TU nested scheme and containing mutex" \
    "scheme-mutex-precedence-local.c:27:.*protected member 'inner.value' modified without holding 'lock'" \
    scheme-mutex-precedence-local.out

run_capture "exact scheme replaces mutex" \
    scheme-mutex-precedence-exact-1.out "$LOCKLINT" \
    scheme-mutex-precedence-exact-mutex-first.c
reject_match "exact scheme replaces mutex" \
    "warning:" scheme-mutex-precedence-exact-1.out

run_capture "exact mutex replaces scheme" \
    scheme-mutex-precedence-exact-2.out "$LOCKLINT" \
    scheme-mutex-precedence-exact-scheme-first.c
require_match "exact mutex replaces scheme" \
    "scheme-mutex-precedence-exact-scheme-first.c:27:.*protected member 'inner.value' modified without holding 'lock'" \
    scheme-mutex-precedence-exact-2.out

#
# Verify that structure policy stops at pointer-member boundaries.
#
run_capture "pointer member data policy" pointer-member-policy.out \
    "$LOCKLINT" pointer-member-policy.c
if [ "$(grep -c 'warning:.*\[unprotected-access\]$' \
    pointer-member-policy.out)" -ne 1 ]; then
	fail "pointer member data policy: expected one unprotected access"
fi
reject_match "pointer pointee policy" \
    "pointer-member-policy.c:45:.*warning:" \
    pointer-member-policy.out
require_match "pointer member write policy" \
    "pointer-member-policy.c:46:.*protected member 'buffer'" \
    pointer-member-policy.out
reject_match "locked pointer pointee policy" \
    "pointer-member-policy.c:49:.*warning:" \
    pointer-member-policy.out

#
# Verify OSLL-compatible type-scoped protection across separately allocated
# objects.  A held lock with the declared canonical member role protects the
# child type regardless of owner instance or pointer provenance.
#
run_capture "cross-object lock role" cross-object-protection.out \
    "$LOCKLINT" cross-object-protection.c
if [ "$(grep -c 'warning:.*\[unprotected-access\]$' \
    cross-object-protection.out)" -ne 8 ]; then
	fail "cross-object lock role: expected eight unlocked accesses"
fi
for line in 66 83 100 118 137 138 156 161
do
	require_match "cross-object unlocked line $line" \
	    "cross-object-protection.c:$line:.*warning:" \
	    cross-object-protection.out
done
for line in 68 85 102 120 140 141 158
do
	reject_match "cross-object role-protected line $line" \
	    "cross-object-protection.c:$line:.*warning:" \
	    cross-object-protection.out
done
run_capture "cross-object protection states" \
    cross-object-protection-states.out "$LOCKLINT" \
    --dump-protection-states --dump-statistics cross-object-protection.c
require_match "cross-object unlocked protection state" \
    "cross-object-protection.c:100:.*states=1 lock=0 invisible=0 no-competition=0 conditional=0 unprotected=1 role=0" \
    cross-object-protection-states.out
require_match "cross-object role protection state" \
    "cross-object-protection.c:102:.*states=1 lock=0 invisible=0 no-competition=0 conditional=0 unprotected=0 role=1" \
    cross-object-protection-states.out
require_match "cross-object role match count" \
    "^statistics protected_lock_role_matches 6$" \
    cross-object-protection-states.out
require_match "cross-object role conflict count" \
    "^statistics lock_identity_role_conflicts 0$" \
    cross-object-protection-states.out

#
# Verify role-only data protection declared in source and by command.  The
# selected role protects only the named member; other members retain exact
# instance protection from their MUTEX_PROTECTS_DATA declarations.
#
run_capture "lock role protects data" lock-role-protection.out \
    "$LOCKLINT" --dump-annotations \
    --dump-protection-states --dump-statistics \
    --cf lock-role-protection.cf lock-role-protection.c
if [ "$(grep -c 'warning:.*\[unprotected-access\]$' \
    lock-role-protection.out)" -ne 5 ]; then
	fail "lock role protects data: expected five unprotected accesses"
fi
for line in 56 59 71 74 114
do
	require_match "lock role unprotected line $line" \
	    "lock-role-protection.c:$line:.*warning:" \
	    lock-role-protection.out
done
for line in 58 73 103 123
do
	reject_match "lock role protected line $line" \
	    "lock-role-protection.c:$line:.*warning:" \
	    lock-role-protection.out
	require_match "lock role protection state line $line" \
	    "lock-role-protection.c:$line:.*states=1 lock=0 invisible=0 no-competition=0 conditional=0 unprotected=0 role=1" \
	    lock-role-protection.out
done
require_match "source lock role annotation" \
    "LOCK_ROLE_PROTECTS_DATA annotation_role_object_t::lock -> annotation_role_object_t::value" \
    lock-role-protection.out
require_match "command lock role annotation" \
    "LOCK_ROLE_PROTECTS_DATA command_role_object_t::lock -> command_role_object_t::value" \
    lock-role-protection.out
require_match "lock role match count" \
    "^statistics protected_lock_role_matches 4$" \
    lock-role-protection.out
require_match "lock role read mode mismatch" \
    "lock-role-protection.c:114:.*required lock 'lock' is read-held; write-holding is required" \
    lock-role-protection.out

run_failure "empty lock role command" lock-role-empty.out \
    "$LOCKLINT" --cf lock-role-empty.cf \
    lock-role-protection.c
require_match "empty lock role command" \
    "lock-role-protects-data requires one lock role and at least one data name" \
    lock-role-empty.out

run_failure "lock role without data" lock-role-no-data.out \
    "$LOCKLINT" --cf lock-role-no-data.cf \
    lock-role-protection.c
require_match "lock role without data" \
    "lock-role-protects-data requires one lock role and at least one data name" \
    lock-role-no-data.out

run_failure "invalid lock role name" lock-role-invalid-lock.out \
    "$LOCKLINT" --cf lock-role-invalid-lock.cf \
    lock-role-protection.c
require_match "invalid lock role name" \
    "invalid lock role name 'bad-name'" lock-role-invalid-lock.out

run_failure "unresolved lock role name" lock-role-unresolved-lock.out \
    "$LOCKLINT" --cf lock-role-unresolved-lock.cf \
    lock-role-protection.c
require_match "unresolved lock role name" \
    "unresolved lock role name 'missing_role_type::lock'" \
    lock-role-unresolved-lock.out

run_failure "invalid lock role data name" lock-role-invalid-data.out \
    "$LOCKLINT" --cf lock-role-invalid-data.cf \
    lock-role-protection.c
require_match "invalid lock role data name" \
    "invalid data name 'bad-name'" lock-role-invalid-data.out

run_failure "unresolved lock role data name" \
    lock-role-unresolved-data.out "$LOCKLINT" \
    --cf lock-role-unresolved-data.cf \
    lock-role-protection.c
require_match "unresolved lock role data name" \
    "unresolved data name 'command_role_object_t::missing'" \
    lock-role-unresolved-data.out

run_failure "inconsistent lock role type" lock-role-inconsistent.out \
    "$LOCKLINT" --cf lock-role-inconsistent.cf \
    type-name-first.c type-name-second-different.c
require_match "inconsistent lock role type" \
    "inconsistently defined type in lock role name 'repeated_name::value'" \
    lock-role-inconsistent.out

run_failure "ambiguous lock role type" lock-role-ambiguous.out \
    "$LOCKLINT" --cf lock-role-ambiguous.cf \
    readable.c readable-other.c
require_match "ambiguous lock role type" \
    "ambiguous lock role name 'duplicate_command_type::value'" \
    lock-role-ambiguous.out

#
# Verify that type-scoped policy declared for one exact header type applies to
# the corresponding exact type in another translation unit.
#
run_capture "canonical cross-TU policy" canonical-policy.out \
    "$LOCKLINT" --dump-statistics \
    canonical-policy-declaration.c \
    canonical-policy-use.c
require_match "canonical cross-TU write protection" \
    "canonical-policy-use.c:.*protected member 'value' modified without holding 'lock'" \
    canonical-policy.out
if [ "$(grep -c '\[unprotected-access\]' canonical-policy.out)" -ne 1 ]; then
	fail "canonical cross-TU policy: expected one unprotected access"
fi
policy_queries=$(sed -n 's/^statistics data_policy_queries //p' \
    canonical-policy.out)
policy_candidates=$(sed -n 's/^statistics data_policy_candidates //p' \
    canonical-policy.out)
if [ -z "$policy_queries" ] || [ "$policy_queries" -eq 0 ]; then
	fail "canonical policy index: expected policy queries"
elif [ -z "$policy_candidates" ] || [ "$policy_candidates" -eq 0 ]; then
	fail "canonical policy index: expected indexed candidates"
elif [ "$policy_candidates" -ge $((policy_queries * 4)) ]; then
	fail "canonical policy index: expected fewer candidates than full scans"
fi
run_capture "canonical external-lock policy references" \
    canonical-policy-annotations.out "$LOCKLINT" --dump-annotations \
    canonical-policy-declaration.c canonical-policy-use.c
if [ "$(grep -c \
    'MUTEX_PROTECTS_DATA canonical_policy_global_lock -> canonical_policy_state::global_value' \
    canonical-policy-annotations.out)" -ne 1 ]; then
	fail "canonical external-lock policy references: expected one reference"
fi
if [ "$(grep -c \
    'MUTEX_PROTECTS_DATA canonical_policy_local_lock -> canonical_policy_state::local_value' \
    canonical-policy-annotations.out)" -ne 2 ]; then
	fail "canonical local-lock policy references: expected two references"
fi

#
# Verify that an exact address derived from formal arguments maps to the
# caller-held lock, while a different dynamic index remains unprotected.
#
run_capture "derived formal protection" derived-formal-protection.out \
    "$LOCKLINT" derived-formal-protection.c
require_empty "derived formal protection" derived-formal-protection.out

run_capture "distinct derived formal protection" \
    derived-formal-protection-different.out "$LOCKLINT" \
    -DDERIVED_FORMAL_DIFFERENT derived-formal-protection.c
if [ "$(grep -c 'warning:.*\[unprotected-access\]$' \
    derived-formal-protection-different.out)" -ne 2 ]; then
	fail "distinct derived formal protection: expected two warnings"
fi
require_match "distinct derived formal caller" \
    "derived-formal-protection.c:62:31: protection was not established at this call to 'derived_helper'" \
    derived-formal-protection-different.out

#
# Verify caller lock state through direct, wrapped, and recursive calls.
#
run_capture "basic call protection diagnostics" calls-basic-diagnostics.out \
    "$LOCKLINT" calls-basic.c
require_match "unlocked direct call" \
    "calls-basic.c:49:22: warning: protected member 'direct_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked wrapped call" \
    "calls-basic.c:55:22: warning: protected member 'transitive_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked recursive call" \
    "calls-basic.c:69:22: warning: protected member 'recursive_value' read without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked first aggregate leaf" \
    "calls-basic.c:128:14: warning: protected member 'pair.first' modified without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
require_match "unlocked second aggregate leaf" \
    "calls-basic.c:128:14: warning: protected member 'pair.second' modified without holding 'lock' \\[unprotected-access\\]" \
    calls-basic-diagnostics.out
if [ "$(grep -c 'warning:' calls-basic-diagnostics.out)" -ne 5 ]; then
	fail "basic call protection diagnostics: expected exactly five warnings"
fi

#
# Verify that recursive call cycles without a syntactic base path are seeded
# as potentially returning, so analysis after the calls reaches a fixed point.
#
run_capture "unconditional recursion diagnostics" \
    unconditional-recursion.out \
    "$LOCKLINT" unconditional-recursion.c
require_match "access after unconditional self recursion" \
    "unconditional-recursion.c:63:.*warning: protected member 'after_self' modified without holding 'lock' \\[unprotected-access\\]" \
    unconditional-recursion.out
require_match "access after unconditional mutual recursion" \
    "unconditional-recursion.c:70:.*warning: protected member 'after_mutual' modified without holding 'lock' \\[unprotected-access\\]" \
    unconditional-recursion.out
if [ "$(grep -c 'warning:' unconditional-recursion.out)" -ne 2 ]; then
	fail "unconditional recursion diagnostics: expected exactly two warnings"
fi

#
# Verify exact computed object identities for common alias forms.
#
run_capture "computed object alias diagnostics" identity-aliases-diagnostics.out \
    "$LOCKLINT" identity-aliases.c
require_match "different copied pointer" \
    "identity-aliases.c:83:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different constant array element" \
    "identity-aliases.c:115:26: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different symbolic array element" \
    "identity-aliases.c:138:31: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
require_match "different recovered container" \
    "identity-aliases.c:173:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-aliases-diagnostics.out
if [ "$(grep -c 'warning:' identity-aliases-diagnostics.out)" -ne 4 ]; then
	fail "computed object alias diagnostics: expected exactly four warnings"
fi

#
# An aggregate mutex protects its indexed inline storage without conflating
# different aggregate instances or different elements of an aggregate array.
#
run_capture "array owner protection" array-owner-protection.out \
    "$LOCKLINT" array-owner-protection.c
reject_match "same owner scalar array" \
    "array-owner-protection.c:55:.*\\[unprotected-access\\]" \
    array-owner-protection.out
reject_match "same owner nested array" \
    "array-owner-protection.c:66:.*\\[unprotected-access\\]" \
    array-owner-protection.out
require_match "different array owner" \
    "array-owner-protection.c:78:.*warning: protected member 'values' read without holding 'lock' \\[unprotected-access\\]" \
    array-owner-protection.out
require_match "different aggregate array element" \
    "array-owner-protection.c:90:.*warning: protected member 'items.value' read without holding 'lock' \\[unprotected-access\\]" \
    array-owner-protection.out
if [ "$(grep -c 'warning:' array-owner-protection.out)" -ne 2 ]; then
	fail "array owner protection: expected exactly two warnings"
fi

#
# Verify formal-to-actual identity for same and different caller objects.
#
run_capture "same formal actual identities" identity-formals-same.out \
    "$LOCKLINT" identity-formals.c
require_empty "same formal actual identities" identity-formals-same.out

run_capture "different formal actual identities" identity-formals-different.out \
    "$LOCKLINT" -DFORMAL_ALIAS_DIFFERENT identity-formals.c
require_match "different direct and wrapped formal actual" \
    "identity-formals.c:57:21: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-formals-different.out
require_match "different independent formal actual" \
    "identity-formals.c:90:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    identity-formals-different.out
if [ "$(grep -c 'warning:' identity-formals-different.out)" -ne 2 ]; then
	fail "different formal actual identities: expected exactly two warnings"
fi

#
# Verify state and identity propagation across translation units.
#
run_capture "cross translation unit diagnostics" cross-diagnostics.out \
    "$LOCKLINT" cross-caller.c cross-callee.c
require_match "cross translation unlocked access" \
    "cross-callee.c:25:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    cross-diagnostics.out
require_match "cross translation held on return" \
    "cross-callee.c:31:22: warning: lock 'lock' held on return from 'cross_acquire' \\[lock-held-on-return\\]" \
    cross-diagnostics.out
if [ "$(grep -c 'warning:' cross-diagnostics.out)" -ne 2 ]; then
	fail "cross translation unit diagnostics: expected exactly two warnings"
fi

#
# Verify protected access through an exactly resolved indirect call.
#
run_capture "exact indirect call diagnostics" indirect-call-diagnostics.out \
    "$LOCKLINT" indirect-calls.c
require_match "unlocked exact indirect call" \
    "indirect-calls.c:43:22: warning: protected member 'value' read without holding 'lock' \\[unprotected-access\\]" \
    indirect-call-diagnostics.out
if [ "$(grep -c 'warning:' indirect-call-diagnostics.out)" -ne 1 ]; then
	fail "exact indirect call diagnostics: expected exactly one warning"
fi

#
# Verify NOT_REACHED removes terminated paths from lock-state merges.
#
run_capture "not reached diagnostics" not-reached-diagnostics.out \
    "$LOCKLINT" not-reached.c
require_match "live unlocked path diagnostic" \
    "not-reached.c:79:14: warning: protected member 'value' modified without holding 'lock' \\[unprotected-access\\]" \
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
require_match "context counting" '^point-states created 16 reused 2$' \
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
    '^statistics cfg_point_states_find 16$' context-statistics.out
require_match "context counting fast-forwarded instructions" \
    '^statistics fast_forwarded_instructions 23$' \
    context-statistics.out
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
    fast_forwarded_instructions \
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
    type_registry_comparisons \
    data_policy_queries \
    data_policy_candidates \
    lock_identity_roles_recorded \
    lock_identity_role_conflicts \
    protected_lock_role_queries \
    protected_lock_role_matches
do
	require_match "context statistic $statistic" \
	    "^statistics $statistic [0-9][0-9]*$" context-statistics.out
done
if [ "$(grep -c '^statistics [a-z_]* [0-9][0-9]*$' \
    context-statistics.out)" -ne 80 ]; then
	fail "context statistics: expected exactly eighty statistics lines"
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
require_match "context calls" '^point-states created 64 reused 3$' \
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
    '^distribution point-states/context samples 9 total 64 max 17$' \
    context-calls.out
require_match "context calls histogram maximum bar" \
    '^           4-7 |\*\{26\}              | 2$' context-calls.out
require_match "context calls histogram scaled bar" \
    '^         16-31 |\*\{13\}                           | 1$' \
    context-calls.out
require_match "context calls" \
    '^distribution states/analysis-point samples 64 total 64 max 1$' \
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
# The protection inventory distinguishes consistent, inconsistent, varying,
# absent, read-only, readers-writer, and noncompeting observations.
#
: > audit-protection.out
for variant in 1 2 3 4 5 6 7 8
do
	echo "--- variant $variant" >> audit-protection.out
	if ! "$LOCKLINT" --no-diagnostics --audit-protection \
	    -DCONSISTENT_PROTECTION_VARIANT="$variant" \
	    consistent-protection.c >> audit-protection.out 2>&1; then
		fail "protection inventory variant $variant: command failed"
	fi
done
compare "protection inventory" audit-protection.ref audit-protection.out

run_capture "structural protection inventory" \
    audit-protection-structural.out "$LOCKLINT" --no-diagnostics \
    --audit-protection data-policy.c
require_match "structural protection datum" \
    '^policy_state::protected[	]' audit-protection-structural.out
require_match "additive protection policy" \
    '^policy_state::read_only[	].*mutex+read-only(note6)[	]' \
    audit-protection-structural.out
reject_match "redundant structural offset" \
    '^policy_state::[^	]*+[0-9]' audit-protection-structural.out

run_capture "readable protection inventory" \
    audit-protection-readable.out "$LOCKLINT" --no-diagnostics \
    --audit-protection readable-protection.c
require_match "readable write-side protection" \
    '^readable_state::value	read/write	mutex+readable	readable_state.lock$' \
    audit-protection-readable.out

#
# The detailed audit expands every note-marked inventory entry into
# deterministic source evidence and honors its site limit.
#
: > audit-unprotected.out
for variant in 1 3 4
do
	echo "--- variant $variant" >> audit-unprotected.out
	if ! "$LOCKLINT" --no-diagnostics --audit-unprotected \
	    -DCONSISTENT_PROTECTION_VARIANT="$variant" \
	    consistent-protection.c >> audit-unprotected.out 2>&1; then
		fail "unprotected audit variant $variant: command failed"
	fi
done
compare "unprotected audit" audit-unprotected.ref audit-unprotected.out

run_capture "limited unprotected audit" audit-unprotected-limit.out \
    "$LOCKLINT" --no-diagnostics --audit-unprotected \
    --audit-site-limit=1 -DCONSISTENT_PROTECTION_VARIANT=1 \
    consistent-protection.c
require_match "limited unprotected site" \
    '    (and 1 more access site)' audit-unprotected-limit.out

run_failure "site limit without detailed audit" \
    audit-site-limit-invalid.out "$LOCKLINT" --no-diagnostics \
    --audit-site-limit=1 consistent-protection.c
require_match "site limit requires detailed audit" \
    'audit-site-limit requires --audit-unprotected' \
    audit-site-limit-invalid.out

run_capture "all unprotected audit sites" audit-unprotected-all.out \
    "$LOCKLINT" --no-diagnostics --audit-unprotected \
    --audit-site-limit=all rwlock.c
require_match "unsuitable rwlock mode detail" \
    'rwlock.c:115 .*written while holding rwlock_state.lock with an unsuitable mode in at least one state' \
    audit-unprotected-all.out

run_capture "unresolved unprotected audit" \
    audit-unprotected-unresolved.out "$LOCKLINT" --no-diagnostics \
    --audit-unprotected array-owner-protection.c
require_match "unresolved held-lock detail" \
    '^array_owner::values - held-lock relationship could not be resolved$' \
    audit-unprotected-unresolved.out

run_capture "separate audit destinations" audit-combined-stdout.out \
    "$LOCKLINT" --no-diagnostics \
    --audit-protection=audit-protection-path.out \
    --audit-unprotected=audit-unprotected-path.out \
    -DCONSISTENT_PROTECTION_VARIANT=3 consistent-protection.c
require_empty "separate audit destination stdout" audit-combined-stdout.out
require_match "separate protection destination" \
    'mutex(note1)' audit-protection-path.out
require_match "separate unprotected destination" \
    'written with no lock held' audit-unprotected-path.out

#
# Final report
#
if [ "$failures" -ne 0 ]; then
	echo "$failures locklint test(s) failed" >&2
	exit 1
fi

echo "All locklint tests passed"

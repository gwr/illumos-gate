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
# Capture OSLL protection and diagnostic evidence for the complete wc module
# from retained databases.  The runner works from the repository root, never
# compiles source, and refuses to replace prior result files.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
LOCK_LINT="$SUNPRO_BIN/lock_lint"
REFERENCE_NAME=osll-diagnostics/accepted-reference.out
OSLL_ROOTS='
wscons.c:wcuwput
wscons.c:wcopen
wscons.c:wclrput
wscons.c:wc_polled_enter
wscons.c:wc_polled_exit
wscons.c:wc_polled_getchar
wscons.c:wc_polled_ischar
wscons.c:wc_polled_putchar
wscons.c:wcclose
wscons.c:wcreioctl
wscons.c:wcrstrt
wscons.c:wc_modechg_cb
vcons.c:vc_avl_compare
'
OSLL_ADAPTER_ROOTS='
wscons.c:wc_attach
wscons.c:wc_info
wscons.c:wcuwsrv
:vt_send_hotkeys
'
OSLL_READABLE='
:wc_dip
:vc_active_console
:vc_cons_user
vc_state::vc_flags
'
OSLL_IGNORED='
vc_state::vc_acqsig
vc_state::vc_bufcallid
vc_state::vc_dispnum
vc_state::vc_login
vc_state::vc_minor
vc_state::vc_pid
vc_state::vc_relsig
vc_state::vc_switch_mode
vc_state::vc_switchto
vc_state::vc_tem
vc_state::vc_timeoutid
vc_state::vc_ttycommon.t_iocpending
vc_state::vc_ttycommon.t_readq
vc_state::vc_ttycommon.t_writeq
vc_state::vc_waitv
vc_state::vc_wq
'
OSLL_EXPECTED_DUPLICATES='function ystm.h:sum_overflows_u16 already loaded;
skipping this definition [systm.h,332]
function ystm.h:sum_overflows_hrtime already loaded;
skipping this definition [systm.h,338]
function ystm.h:sum_overflows_off already loaded;
skipping this definition [systm.h,345]'

#
# Load one retained database and preserve both its output and exact status.
# The second production database has three known duplicate inline definitions.
#
run_load()
{
	typeset file=$1
	typeset name=$2
	typeset allow_duplicates=$3
	typeset output
	typeset status

	"$LOCK_LINT" load "$file" >"$OSLL_DIAGNOSTICS_DIR/$name.out" 2>&1
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/$name.status"
	output=$(<"$OSLL_DIAGNOSTICS_DIR/$name.out")

	case "$status" in
	0)
		return 0
		;;
	2)
		if [[ "$allow_duplicates" = yes &&
		    "$output" = "$OSLL_EXPECTED_DUPLICATES" ]]; then
			return 0
		fi
		;;
	esac

	print -u2 "OSLL failed to load $file with status $status"
	if [[ -n "$output" ]]; then
		print -u2 -- "$output"
	fi
	return "$status"
}

#
# Verify that compilation retained a known source annotation.  An
# annotation-free database must not pass because the diagnostic report is
# otherwise empty.
#
verify_source_annotations()
{
	typeset output
	typeset status

	"$LOCK_LINT" vars -a vc_state::vc_flags \
	    >"$OSLL_DIAGNOSTICS_DIR/annotation.out" 2>&1
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/annotation.status"
	output=$(<"$OSLL_DIAGNOSTICS_DIR/annotation.out")

	if [[ "$status" -ne 0 ]] ||
	    ! print -- "$output" | grep -q \
	    '^vc_state::vc_flags[[:space:]].*assert=vc_state::vc_state_lock$'
	then
		print -u2 "OSLL did not retain the vc_flags protection annotation"
		return 1
	fi
}

#
# Apply the accepted wc roots and data policy without producing routine
# command output.
#
configure_session()
{
	typeset object
	typeset root
	typeset variable

	for root in $OSLL_ROOTS
	do
		"$LOCK_LINT" declare root "$root" || return $?
	done
	for root in $OSLL_ADAPTER_ROOTS
	do
		"$LOCK_LINT" declare root "$root" || return $?
	done
	for variable in $OSLL_READABLE
	do
		"$LOCK_LINT" declare readable "$variable" || return $?
	done
	for object in $OSLL_IGNORED
	do
		"$LOCK_LINT" ignore "$object" || return $?
	done
}

#
# Reduce OSLL output to the actionable diagnostics used by the accepted
# historical comparison.
#
write_summary()
{
	typeset diagnostics
	typeset unanalyzed

	unanalyzed=$(awk '
	    /=unanalyzed/ &&
	    ($0 ~ /\[wscons.c,/ || $0 ~ /\[vcons.c,/) {
		name = $1
		print "unanalyzed module function: " name
	    }
	' "$FUNCTIONS_RAW")

	diagnostics=$(awk '
	    /^The following functions are clearly supposed to be roots/ {
		roots = 1
		next
	    }
	    roots && NF == 0 {
		roots = 0
		next
	    }
	    roots {
		next
	    }
	    /^\* Warning: calls made to the following function have not been analyzed\.$/ {
		external = 1
		next
	    }
	    external && /^  function = / {
		external = 0
		next
	    }
	    NF != 0 {
		print
	    }
	' "$ANALYZE_RAW")

	if [[ -n "$unanalyzed" ]]; then
		print -- "$unanalyzed"
	fi
	if [[ -n "$diagnostics" ]]; then
		print -- "$diagnostics"
	fi
}

#
# Run inside one lock_lint session so all databases, declarations, analysis,
# and inventories share the same analyzer state.
#
run_session()
{
	typeset status

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi
	cd "$REPO_ROOT" || exit 1

	run_load "$OSLL_WSCONS_LL" load-wscons no || exit $?
	run_load "$OSLL_VCONS_LL" load-vcons yes || exit $?
	run_load "$OSLL_ADAPTER_LL" load-adapter no || exit $?
	verify_source_annotations || exit $?

	configure_session >"$OSLL_DIAGNOSTICS_DIR/configure.out" 2>&1
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/configure.status"
	if [[ "$status" -ne 0 ]]; then
		exit "$status"
	fi

	"$LOCK_LINT" analyze >"$ANALYZE_RAW" 2>&1
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/analyze.status"
	case "$status" in
	0|5)
		;;
	*)
		exit "$status"
		;;
	esac

	"$LOCK_LINT" funcs -o >"$FUNCTIONS_RAW" 2>&1
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/functions.status"
	if [[ "$status" -ne 0 ]]; then
		exit "$status"
	fi

	"$LOCK_LINT" vars -a >"$VARS_ALL_RAW" 2>&1
	status=$?
	print -- "$status" >"$OSLL_PROTECTION_DIR/vars-a.status"
	if [[ "$status" -ne 0 ]]; then
		exit "$status"
	fi

	"$LOCK_LINT" vars -h >"$VARS_HELD_RAW" 2>&1
	status=$?
	print -- "$status" >"$OSLL_PROTECTION_DIR/vars-h.status"
	if [[ "$status" -ne 0 ]]; then
		exit "$status"
	fi

	write_summary >"$REPORT_RAW"
	status=$?
	print -- "$status" >"$OSLL_DIAGNOSTICS_DIR/report.status"
	exit "$status"
}

if [[ "${OSLL_AUDIT_SESSION:-0}" = 1 ]]; then
	run_session
fi

if [[ "$#" -ne 1 ]]; then
	print -u2 "usage: $0 repo-relative-result-directory"
	exit 2
fi
RESULT_DIR=$1
case "$RESULT_DIR" in
/*)
	print -u2 "result directory must be relative to the repository root"
	exit 2
	;;
esac

cd "$REPO_ROOT" || exit 1
OSLL_PROTECTION_DIR="$RESULT_DIR/osll-protection"
OSLL_DIAGNOSTICS_DIR="$RESULT_DIR/osll-diagnostics"
OSLL_WSCONS_LL="$OSLL_PROTECTION_DIR/ll/wscons.ll"
OSLL_VCONS_LL="$OSLL_PROTECTION_DIR/ll/vcons.ll"
OSLL_ADAPTER_LL="$OSLL_PROTECTION_DIR/ll/check-kmod-wc-adapter.ll"
ANALYZE_RAW="$OSLL_DIAGNOSTICS_DIR/analyze.out"
FUNCTIONS_RAW="$OSLL_DIAGNOSTICS_DIR/functions.out"
REPORT_RAW="$OSLL_DIAGNOSTICS_DIR/report.out"
VARS_ALL_RAW="$OSLL_PROTECTION_DIR/vars-a.out"
VARS_HELD_RAW="$OSLL_PROTECTION_DIR/vars-h.out"

for file in "$OSLL_WSCONS_LL" "$OSLL_VCONS_LL" "$OSLL_ADAPTER_LL" \
    "$RESULT_DIR/$REFERENCE_NAME"
do
	if [[ ! -f "$file" ]]; then
		print -u2 "required input not found: $file"
		exit 1
	fi
done

for file in "$OSLL_PROTECTION_DIR/session.out" \
    "$OSLL_PROTECTION_DIR/session.status" "$VARS_ALL_RAW" "$VARS_HELD_RAW" \
    "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$REPORT_RAW"
do
	if [[ -e "$file" ]]; then
		print -u2 "refusing to overwrite result: $file"
		exit 1
	fi
done

if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi

TMPDIR="$OSLL_PROTECTION_DIR/tmp"
mkdir -p "$TMPDIR" || exit 1
OSLL_AUDIT_SESSION=1
export LOCK_LINT OSLL_AUDIT_SESSION OSLL_RESULT_DIR
export OSLL_PROTECTION_DIR OSLL_DIAGNOSTICS_DIR
export OSLL_WSCONS_LL OSLL_VCONS_LL OSLL_ADAPTER_LL
export ANALYZE_RAW FUNCTIONS_RAW REPORT_RAW VARS_ALL_RAW VARS_HELD_RAW TMPDIR

"$LOCK_LINT" start "$SCRIPT" >"$OSLL_PROTECTION_DIR/session.out" 2>&1
status=$?
print -- "$status" >"$OSLL_PROTECTION_DIR/session.status"
if [[ "$status" -ne 0 ]]; then
	print -u2 "OSLL wc audit session failed with status $status"
	print -u2 "see $OSLL_PROTECTION_DIR/session.out"
	exit "$status"
fi

if cmp -s "$RESULT_DIR/$REFERENCE_NAME" "$REPORT_RAW"; then
	print 0 >"$OSLL_DIAGNOSTICS_DIR/reference.status"
else
	diff -u "$RESULT_DIR/$REFERENCE_NAME" "$REPORT_RAW" \
	    >"$OSLL_DIAGNOSTICS_DIR/reference.diff"
	print 1 >"$OSLL_DIAGNOSTICS_DIR/reference.status"
	print -u2 "OSLL wc diagnostics differ from the accepted reference"
	exit 1
fi

print "OSLL wc audit results: $RESULT_DIR"

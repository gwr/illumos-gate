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
# Run Solaris LockLint over the complete wc kernel module.  The compiler
# arguments are the Sun-style inputs recorded in the wc build's cw command,
# excluding options routed only to GCC or Smatch.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-wc.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-wc.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/wc"
WORK_DIR="$MODULE_DIR/osll"

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "wc module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
FUNCTIONS_RAW="$WORK_DIR/functions.raw"
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

run_quiet()
{
	"$@" >/dev/null
}

#
# The second database repeats three inline definitions from systm.h.  Accept
# that known partial-load result, but do not hide any other skipped definition.
#
run_load()
{
	file=$1
	allow_duplicates=$2

	load_output=$("$LOCK_LINT" load "$file" 2>&1)
	status=$?
	case "$status" in
	0)
		if [[ -n "$load_output" ]]; then
			print -u2 -- "$load_output"
		fi
		return 0
		;;
	2)
		if [[ "$allow_duplicates" = yes &&
		    "$load_output" = "$OSLL_EXPECTED_DUPLICATES" ]]; then
			print -u2 -- "$load_output"
			return 0
		fi
		print -u2 -- "$load_output"
		return "$status"
		;;
	*)
		print -u2 -- "$load_output"
		return "$status"
		;;
	esac
}

#
# An ordinary source-tree include path finds the no-op sys/note.h before
# SunPro's LockLint annotation header.  Verify that the databases retained a
# known source annotation so an accidentally annotation-free run cannot pass
# merely because its actionable reference is empty.
#
verify_source_annotations()
{
	typeset output

	output=$("$LOCK_LINT" vars -a vc_state::vc_flags 2>&1)
	status=$?
	if [[ "$status" -ne 0 ]] ||
	    ! print -- "$output" | grep -q \
	    '^vc_state::vc_flags[[:space:]].*assert=vc_state::vc_state_lock$'
	then
		print -u2 "OSLL did not retain the vc_flags protection annotation"
		if [[ -n "$output" ]]; then
			print -u2 -- "$output"
		fi
		return 1
	fi
}

#
# Reduce OSLL output to actionable diagnostics.  Root guidance and calls to
# unmodeled external functions are outside this module's locking semantics.
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
# Load and configure the two translation units within one OSLL session, then
# produce the normalized output consumed by the reference comparison.
#
run_session()
{
	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_WSCONS_LL" no || exit $?
	run_load "$OSLL_VCONS_LL" yes || exit $?
	run_load "$OSLL_ADAPTER_LL" no || exit $?
	verify_source_annotations || exit $?

	for root in $OSLL_ROOTS
	do
		run_quiet "$LOCK_LINT" declare root "$root" || exit $?
	done
	for root in $OSLL_ADAPTER_ROOTS
	do
		run_quiet "$LOCK_LINT" declare root "$root" || exit $?
	done

	for variable in $OSLL_READABLE
	do
		run_quiet "$LOCK_LINT" declare readable "$variable" || exit $?
	done

	for object in $OSLL_IGNORED
	do
		run_quiet "$LOCK_LINT" ignore "$object" || exit $?
	done

	"$LOCK_LINT" analyze >"$ANALYZE_RAW" 2>&1
	analyze_status=$?

	case "$analyze_status" in
	0|5)
		"$LOCK_LINT" funcs -o >"$FUNCTIONS_RAW" 2>&1
		coverage_status=$?
		if [[ "$coverage_status" -ne 0 ]]; then
			exit "$coverage_status"
		fi
		write_summary >"$OUTPUT" || exit 1
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac
}

if [[ "${OSLL_SESSION_MODE:-0}" = 1 ]]; then
	run_session
fi

if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi
if [[ ! -d "$SSBD_INCLUDE/sys" ]]; then
	print -u2 "OSLL annotation headers not found: $SSBD_INCLUDE"
	exit 1
fi

if [[ -e wscons.ll || -e vcons.ll ]]; then
	print -u2 "wc module directory already contains an OSLL database"
	print -u2 "rename or remove wscons.ll and vcons.ll before starting"
	exit 1
fi
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW"

#
# Keep the common native compiler arguments in one place.  The source and
# object-output arguments are supplied separately so cc -Zll leaves each
# database in this directory.
#
compile_source()
{
	source=$1
	database=$2

	"$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$WORK_DIR/$database" || return 1
	fi
	return "$status"
}

print "Compiling wc sources for OSLL"
compile_source ../../common/io/wscons.c wscons.ll &&
    compile_source ../../common/io/vcons.c vcons.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-wc-adapter.c" \
    check-kmod-wc-adapter.ll
compile_status=$?
if [[ "$compile_status" -ne 0 ]]; then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_WSCONS_LL="$WORK_DIR/wscons.ll"
OSLL_VCONS_LL="$WORK_DIR/vcons.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-wc-adapter.ll"
if [[ ! -f "$OSLL_WSCONS_LL" || ! -f "$OSLL_VCONS_LL" ||
    ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_SESSION_MODE OSLL_WSCONS_LL OSLL_VCONS_LL OSLL_ADAPTER_LL
export TMPDIR

print "Running OSLL over wc"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if [[ "$session_status" -ne 0 ]]; then
	print -u2 "OSLL wc session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi

if cmp -s "$REFERENCE" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL wc semantic output matches check-kmod-wc.ref."
else
	print -u2 "OSLL wc semantic output differs from check-kmod-wc.ref:"
	diff -u "$REFERENCE" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

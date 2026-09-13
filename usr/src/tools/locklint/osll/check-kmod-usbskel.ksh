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
# Run Solaris LockLint over the complete usbskel kernel module.  Set
# OSLL_ONE_MODE to "with" or "without" to characterize the historical
# "one usbskel_state" declaration while keeping separate raw results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-usbskel.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-usbskel.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbskel"
WORK_DIR="$MODULE_DIR/osll"
OSLL_ONE_MODE=${OSLL_ONE_MODE:-with}

case "$OSLL_ONE_MODE" in
with|without)
	;;
*)
	print -u2 "OSLL_ONE_MODE must be 'with' or 'without'"
	exit 1
	;;
esac

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbskel module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"
ANALYZE_RAW="$WORK_DIR/analyze.$OSLL_ONE_MODE.raw"
FUNCTIONS_RAW="$WORK_DIR/functions.$OSLL_ONE_MODE.raw"
SESSION_OK="$WORK_DIR/session.$OSLL_ONE_MODE.ok"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"

OSLL_HISTORICAL_ROOTS='
usbskel.c:usbskel_normal_callback
usbskel.c:usbskel_exception_callback
usbskel.c:usbskel_disconnect_callback
usbskel.c:usbskel_reconnect_callback
'

#
# Warlock's pseudo-kernel reached these DDI entry points through the module's
# dev_ops and cb_ops structures.  The focused comparison does not load that
# model, so declare the equivalent roots explicitly.
#
OSLL_MODEL_ROOTS='
usbskel.c:usbskel_info
usbskel.c:usbskel_attach
usbskel.c:usbskel_detach
usbskel.c:usbskel_power
usbskel.c:usbskel_open
usbskel.c:usbskel_close
usbskel.c:usbskel_strategy
usbskel.c:usbskel_read
usbskel.c:usbskel_ioctl
'

OSLL_MODULE_FUNCTIONS='
_init
_fini
_info
usbskel_info
usbskel_attach
usbskel_detach
usbskel_cleanup
usbskel_open
usbskel_close
usbskel_read
usbskel_strategy
usbskel_minphys
usbskel_normal_callback
usbskel_exception_callback
usbskel_ioctl
usbskel_disconnect_callback
usbskel_reconnect_callback
usbskel_restore_device_state
usbskel_cpr_suspend
usbskel_cpr_resume
usbskel_test_and_adjust_device_state
usbskel_open_pipes
usbskel_close_pipes
usbskel_pm_busy_component
usbskel_pm_idle_component
usbskel_power
usbskel_serialize_access
usbskel_release_access
usbskel_check_same_device
usbskel_log
'

run_quiet()
{
	"$@" >/dev/null
}

run_load()
{
	file=$1

	load_output=$("$LOCK_LINT" load "$file" 2>&1)
	status=$?
	if [[ -n "$load_output" ]]; then
		print -u2 -- "$load_output"
	fi
	return "$status"
}

verify_source_annotations()
{
	typeset output

	output=$("$LOCK_LINT" vars -a \
	    locklint_usbskel_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_usbskel_annotation_probe::value[[:space:]].*assert=locklint_usbskel_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the usbskel annotation probe"
		if [[ -n "$output" ]]; then
			print -u2 -- "$output"
		fi
		return 1
	fi
}

verify_module_coverage()
{
	typeset function
	typeset missing=
	typeset symbol

	for function in $OSLL_MODULE_FUNCTIONS
	do
		case "$function" in
		_init|_fini|_info)
			symbol=":$function"
			;;
		*)
			symbol="usbskel.c:$function"
			;;
		esac
		if ! grep -q \
		    "^${symbol}[[:space:]].*\\[usbskel.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q '\[usbskel.c,[0-9][0-9]*\].*=unanalyzed' "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left usbskel module functions unanalyzed"
		return 1
	fi
}

write_summary()
{
	awk '
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
	' "$ANALYZE_RAW"
}

run_session()
{
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_USBSKEL_LL" || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?

	if [[ "$OSLL_ONE_MODE" == with ]]; then
		run_quiet "$LOCK_LINT" declare one usbskel_state || exit $?
	fi

	for root in $OSLL_HISTORICAL_ROOTS $OSLL_MODEL_ROOTS
	do
		run_quiet "$LOCK_LINT" declare root "$root" || exit $?
	done

	"$LOCK_LINT" analyze >"$ANALYZE_RAW" 2>&1
	analyze_status=$?

	case "$analyze_status" in
	0|5)
		"$LOCK_LINT" funcs -o >"$FUNCTIONS_RAW" 2>&1
		coverage_status=$?
		if (( coverage_status != 0 )); then
			exit "$coverage_status"
		fi
		verify_module_coverage || exit 1
		write_summary >"$OUTPUT" || exit 1
		print ok >"$SESSION_OK" || exit 1
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac
}

if [[ "${OSLL_SESSION_MODE:-0}" == 1 ]]; then
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

if [[ -e usbskel.ll ]]; then
	print -u2 "usbskel module directory already contains an OSLL database"
	print -u2 "rename or remove usbskel.ll before starting"
	exit 1
fi
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/usbskel.ll" \
    "$WORK_DIR/check-kmod-usbskel-adapter.ll"

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

print "Compiling usbskel sources for OSLL"
compile_source ../../common/io/usb/clients/usbskel/usbskel.c usbskel.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-usbskel-adapter.c" \
    check-kmod-usbskel-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_USBSKEL_LL="$WORK_DIR/usbskel.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-usbskel-adapter.ll"
if [[ ! -f "$OSLL_USBSKEL_LL" || ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_ONE_MODE OSLL_SESSION_MODE OSLL_USBSKEL_LL \
    OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over usbskel ($OSLL_ONE_MODE one declaration)"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL usbskel session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL usbskel session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL usbskel semantic output matches check-kmod-usbskel.ref."
else
	print -u2 "OSLL usbskel semantic output differs from " \
	    "check-kmod-usbskel.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

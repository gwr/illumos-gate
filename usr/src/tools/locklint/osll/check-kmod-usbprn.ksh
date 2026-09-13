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
# Run Solaris LockLint over the standalone usbprn kernel module.  Set
# OSLL_ONE_MODE to "with" or "without" to characterize the historical
# "one usbprn_state" declaration while keeping separate raw results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-usbprn.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-usbprn.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbprn"
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
	print -u2 "usbprn module directory not found: $MODULE_DIR"
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
usbprn.c:usbprn_open
usbprn.c:usbprn_close
usbprn.c:usbprn_ioctl
usbprn.c:usbprn_bulk_xfer_cb
usbprn.c:usbprn_bulk_xfer_exc_cb
usbprn.c:usbprn_reconnect_event_cb
usbprn.c:usbprn_disconnect_event_cb
usbprn.c:usbprn_power
'

#
# Warlock's pseudo-kernel reached these DDI entry points through the module's
# dev_ops and cb_ops structures.  The focused comparison does not load that
# model, so declare the equivalent roots explicitly.  The historical physio
# implementation in the adapter reaches strategy and minphys.
#
OSLL_MODEL_ROOTS='
usbprn.c:usbprn_info
usbprn.c:usbprn_attach
usbprn.c:usbprn_detach
usbprn.c:usbprn_read
usbprn.c:usbprn_write
usbprn.c:usbprn_poll
'

OSLL_MODULE_FUNCTIONS='
_init
_fini
_info
usbprn_info
usbprn_attach
usbprn_detach
usbprn_cleanup
usbprn_cpr_suspend
usbprn_cpr_resume
usbprn_get_descriptors
usbprn_get_device_id
usbprn_get_port_status
usbprn_open
usbprn_close
usbprn_read
usbprn_write
usbprn_poll
usbprn_strategy
usbprn_ioctl
usbprn_minphys
usbprn_open_usb_pipes
usbprn_close_usb_pipes
usbprn_getparms
usbprn_setparms
usbprn_geterr
usbprn_ioctl_get_status
usbprn_testio
usbprn_prnio_get_status
usbprn_prnio_get_1284_status
usbprn_prnio_get_ifcap
usbprn_prnio_set_ifcap
usbprn_prnio_get_ifinfo
usbprn_prnio_get_1284_devid
usbprn_prnio_get_timeouts
usbprn_prnio_set_timeouts
usbprn_biodone
usbprn_send_async_bulk_data
usbprn_bulk_xfer_cb
usbprn_bulk_xfer_exc_cb
usbprn_reconnect_event_cb
usbprn_disconnect_event_cb
usbprn_restore_device_state
usbprn_create_pm_components
usbprn_pwrlvl0
usbprn_pwrlvl1
usbprn_pwrlvl2
usbprn_pwrlvl3
usbprn_power
usbprn_print_long
usbprn_pm_busy_component
usbprn_pm_idle_component
usbprn_error_state
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
	    locklint_usbprn_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_usbprn_annotation_probe::value[[:space:]].*assert=locklint_usbprn_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the usbprn annotation probe"
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
			symbol="usbprn.c:$function"
			;;
		esac
		if ! grep -q \
		    "^${symbol}[[:space:]].*\\[usbprn.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q '\[usbprn.c,[0-9][0-9]*\].*=unanalyzed' "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left usbprn module functions unanalyzed"
		return 1
	fi
}

write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbprn_state::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbprn.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
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

	run_load "$OSLL_USBPRN_LL" || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?

	if [[ "$OSLL_ONE_MODE" == with ]]; then
		run_quiet "$LOCK_LINT" declare one usbprn_state || exit $?
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

if [[ -e usbprn.ll ]]; then
	print -u2 "usbprn module directory already contains an OSLL database"
	print -u2 "rename or remove usbprn.ll before starting"
	exit 1
fi
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/usbprn.ll" \
    "$WORK_DIR/check-kmod-usbprn-adapter.ll"

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

print "Compiling usbprn sources for OSLL"
compile_source ../../common/io/usb/clients/printer/usbprn.c usbprn.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-usbprn-adapter.c" \
    check-kmod-usbprn-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_USBPRN_LL="$WORK_DIR/usbprn.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-usbprn-adapter.ll"
if [[ ! -f "$OSLL_USBPRN_LL" || ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_ONE_MODE OSLL_SESSION_MODE OSLL_USBPRN_LL \
    OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over usbprn ($OSLL_ONE_MODE one declaration)"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL usbprn session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL usbprn session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL usbprn semantic output matches check-kmod-usbprn.ref."
else
	print -u2 "OSLL usbprn semantic output differs from " \
	    "check-kmod-usbprn.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

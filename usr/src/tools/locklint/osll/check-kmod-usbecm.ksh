#!/bin/ksh
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.opensolaris.org/os/licensing.
#

#
# Run Solaris LockLint over the standalone usbecm kernel module.  Set
# OSLL_ONE_MODE to "with" or "without" to characterize the historical
# "one usbecm_state" declaration while keeping separate raw results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-usbecm.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-usbecm.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbecm"
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
	print -u2 "usbecm module directory not found: $MODULE_DIR"
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
usbecm.c:usbecm_bulkin_cb
usbecm.c:usbecm_bulkout_cb
usbecm.c:usbecm_intr_cb
usbecm.c:usbecm_intr_ex_cb
usbecm.c:usbecm_pm_set_busy
usbecm.c:usbecm_pm_set_idle
usbecm.c:usbecm_power
usbecm.c:usbecm_m_stop
usbecm.c:usbecm_m_start
usbecm.c:usbecm_m_unicst
usbecm.c:usbecm_m_multicst
usbecm.c:usbecm_m_promisc
usbecm.c:usbecm_m_ioctl
usbecm.c:usbecm_m_tx
usbecm.c:usbecm_m_getprop
usbecm.c:usbecm_m_setprop
usbecm.c:usbecm_m_stat
usbecm.c:usbecm_disconnect_event_cb
usbecm.c:usbecm_reconnect_event_cb
'

#
# Warlock's pseudo-kernel reached these DDI entry points through dev_ops.
# The focused comparison does not load that model, so declare them explicitly.
#
OSLL_MODEL_ROOTS='
usbecm.c:usbecm_attach
usbecm.c:usbecm_detach
'

OSLL_MODULE_FUNCTIONS='
_init
_fini
_info
generate_ether_addr
label_to_mac
usbecm_attach
usbecm_bulkin_cb
usbecm_bulkout_cb
usbecm_cleanup
usbecm_close_pipes
usbecm_create_pm_components
usbecm_ctrl_read
usbecm_ctrl_write
usbecm_destroy_pm_components
usbecm_detach
usbecm_disconnect_event_cb
usbecm_find_bulk_in_out_eps
usbecm_get_descriptors
usbecm_get_statistics
usbecm_init_non_compatible_device
usbecm_intr_cb
usbecm_intr_ex_cb
usbecm_is_compatible
usbecm_m_getprop
usbecm_m_ioctl
usbecm_m_multicst
usbecm_m_promisc
usbecm_m_setprop
usbecm_m_start
usbecm_m_stat
usbecm_m_stop
usbecm_m_tx
usbecm_m_unicst
usbecm_mac_fini
usbecm_mac_init
usbecm_open_pipes
usbecm_parse_intr_data
usbecm_pipe_start_polling
usbecm_pm_set_busy
usbecm_pm_set_idle
usbecm_power
usbecm_pwrlvl0
usbecm_pwrlvl1
usbecm_pwrlvl2
usbecm_pwrlvl3
usbecm_reconnect_event_cb
usbecm_restore_device_state
usbecm_resume
usbecm_rx_start
usbecm_send_data
usbecm_send_zero_data
usbecm_suspend
usbecm_usb_init
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
	    locklint_usbecm_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_usbecm_annotation_probe::value[[:space:]].*assert=locklint_usbecm_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the usbecm annotation probe"
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
		_init|_fini|_info|label_to_mac|usbecm_find_bulk_in_out_eps)
			symbol=":$function"
			;;
		*)
			symbol="usbecm.c:$function"
			;;
		esac
		if ! grep -q \
		    "^${symbol}[[:space:]].*\\[usbecm.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q '\[usbecm.c,[0-9][0-9]*\].*=unanalyzed' "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left usbecm module functions unanalyzed"
		return 1
	fi
}

write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbecm_state::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbecm.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$ANALYZE_RAW"
}

declare_ds_targets()
{
	run_quiet "$LOCK_LINT" declare usbecm_ds_ops::ecm_ds_init targets \
	    :locklint_usbecm_ds_init || return $?
	run_quiet "$LOCK_LINT" declare usbecm_ds_ops::ecm_ds_fini targets \
	    :locklint_usbecm_ds_fini || return $?
	run_quiet "$LOCK_LINT" declare usbecm_ds_ops::ecm_ds_start targets \
	    :locklint_usbecm_ds_start || return $?
	run_quiet "$LOCK_LINT" declare usbecm_ds_ops::ecm_ds_stop targets \
	    :locklint_usbecm_ds_stop || return $?
	run_quiet "$LOCK_LINT" declare usbecm_ds_ops::ecm_ds_intr_cb targets \
	    :locklint_usbecm_ds_intr_cb
}

run_session()
{
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_USBECM_LL" || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?
	declare_ds_targets || exit $?

	if [[ "$OSLL_ONE_MODE" == with ]]; then
		run_quiet "$LOCK_LINT" declare one usbecm_state || exit $?
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

if [[ -e usbecm.ll ]]; then
	print -u2 "usbecm module directory already contains an OSLL database"
	print -u2 "rename or remove usbecm.ll before starting"
	exit 1
fi
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/usbecm.ll" \
    "$WORK_DIR/check-kmod-usbecm-adapter.ll"

compile_source()
{
	source=$1
	database=$2

	"$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -D__lock_lint=1 \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$WORK_DIR/$database" || return 1
	fi
	return "$status"
}

print "Compiling usbecm sources for OSLL"
compile_source ../../common/io/usb/clients/usbecm/usbecm.c usbecm.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-usbecm-adapter.c" \
    check-kmod-usbecm-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_USBECM_LL="$WORK_DIR/usbecm.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-usbecm-adapter.ll"
if [[ ! -f "$OSLL_USBECM_LL" || ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_ONE_MODE OSLL_SESSION_MODE OSLL_USBECM_LL \
    OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over usbecm ($OSLL_ONE_MODE one declaration)"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL usbecm session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL usbecm session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL usbecm semantic output matches check-kmod-usbecm.ref."
else
	print -u2 "OSLL usbecm semantic output differs from " \
	    "check-kmod-usbecm.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

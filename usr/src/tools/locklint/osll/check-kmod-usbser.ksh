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
# Run Solaris LockLint over the standalone usbser kernel module.  Set
# OSLL_ONE_MODE to "with" or "without" to characterize the historical
# "one usbser_state" declaration while keeping separate raw results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-usbser.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-usbser.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbser"
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
	print -u2 "usbser module directory not found: $MODULE_DIR"
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
:usbser_first_device
usbser.c:usbser_putchar
usbser.c:usbser_getchar
usbser.c:usbser_ischar
usbser.c:usbser_polledio_enter
usbser.c:usbser_polledio_exit
:usbser_soft_state_size
:usbser_open
:usbser_close
:usbser_wput
:usbser_wsrv
:usbser_rsrv
usbser.c:usbser_tx_cb
usbser.c:usbser_rx_cb
usbser.c:usbser_status_cb
usbser.c:usbser_wq_thread
usbser.c:usbser_rq_thread
usbser.c:usbser_disconnect_cb
usbser.c:usbser_reconnect_cb
usbser.c:usbser_cpr_suspend
usbser.c:usbser_cpr_resume
'

#
# Warlock's pseudo-kernel reached the DDI entry points through dev_ops and
# usbser_restart through timeout(9F).  The focused comparison does not load
# those models, so declare the equivalent roots explicitly.
#
OSLL_MODEL_ROOTS='
:usbser_attach
:usbser_detach
:usbser_getinfo
:usbser_power
usbser.c:usbser_restart
'

OSLL_MODULE_FUNCTIONS='
_init
_fini
_info
usbser_soft_state_size
usbser_getinfo
usbser_insert
usbser_remove
usbser_first_device
usbser_attach
usbser_detach
usbser_open
usbser_close
usbser_rsrv
usbser_wput
usbser_wsrv
usbser_power
usbser_rseq_do_cb
usbser_free_soft_state
usbser_init_soft_state
usbser_fini_soft_state
usbser_attach_dev
usbser_detach_dev
usbser_attach_ports
usbser_create_port_minor_nodes
usbser_detach_ports
usbser_create_taskq
usbser_destroy_taskq
usbser_set_dev_state_init
usbser_disconnect_cb
usbser_reconnect_cb
usbser_disconnect_ports
usbser_cpr_suspend
usbser_suspend_ports
usbser_cpr_resume
usbser_restore_device_state
usbser_restore_ports_state
usbser_open_setup
usbser_open_init
usbser_check_port_props
usbser_open_fini
usbser_open_line_setup
usbser_open_carrier_check
usbser_open_queues_init
usbser_open_queues_fini
usbser_close_drain
usbser_close_cancel_break
usbser_close_hangup
usbser_close_cleanup
usbser_thr_dispatch
usbser_thr_cancel
usbser_thr_wake
usbser_wq_thread
usbser_rq_thread
usbser_tx_cb
usbser_rx_cb
usbser_rx_massage_data
usbser_rx_massage_mbreak
usbser_rx_cb_put
usbser_status_cb
usbser_status_proc_cb
usbser_wmsg
usbser_data
usbser_ioctl
usbser_iocdata
usbser_stop
usbser_start
usbser_stopi
usbser_starti
usbser_flush
usbser_break
usbser_delay
usbser_restart
usbser_port_program
usbser_inbound_flow_ctl
usbser_dev_is_online
usbser_serialize_port_act
usbser_release_port_act
usbser_msgtype2str
usbser_ioctl2str
usbser_polledio_init
usbser_polledio_fini
usbser_putchar
usbser_getchar
usbser_ischar
usbser_polledio_enter
usbser_polledio_exit
rseq_do_common
rseq_undo_common
rseq_do
rseq_undo
'

OSLL_DS_MEMBERS='
ds_attach
ds_detach
ds_register_cb
ds_unregister_cb
ds_open_port
ds_close_port
ds_usb_power
ds_suspend
ds_resume
ds_disconnect
ds_reconnect
ds_set_port_params
ds_set_modem_ctl
ds_get_modem_ctl
ds_break_ctl
ds_loopback
ds_tx
ds_rx
ds_stop
ds_start
ds_fifo_flush
ds_fifo_drain
ds_out_pipe
ds_in_pipe
'

OSLL_RSEQ_TARGETS='
usbser.c:usbser_free_soft_state
usbser.c:usbser_init_soft_state
usbser.c:usbser_fini_soft_state
usbser.c:usbser_attach_dev
usbser.c:usbser_detach_dev
usbser.c:usbser_attach_ports
usbser.c:usbser_detach_ports
usbser.c:usbser_create_taskq
usbser.c:usbser_destroy_taskq
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
	    locklint_usbser_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_usbser_annotation_probe::value[[:space:]].*assert=locklint_usbser_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the usbser annotation probe"
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
		_init|_fini|_info|rseq_do|rseq_undo|usbser_attach|\
		usbser_close|usbser_detach|usbser_first_device|usbser_getinfo|\
		usbser_open|usbser_power|usbser_rsrv|usbser_soft_state_size|\
		usbser_wput|usbser_wsrv)
			symbol=":$function"
			;;
		rseq_do_common|rseq_undo_common)
			symbol="usbser_rseq.c:$function"
			;;
		*)
			symbol="usbser.c:$function"
			;;
		esac
		if ! grep -q \
		    "^${symbol}[[:space:]].*\\[usbser\\(_rseq\\)\\{0,1\\}.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q \
	    '\[usbser\(_rseq\)\{0,1\}.c,[0-9][0-9]*\].*=unanalyzed' \
	    "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left usbser module functions unanalyzed"
		return 1
	fi
}

write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbser_state::/, "", member)
		sub(/^usbser_port::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbser.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$ANALYZE_RAW"
}

declare_indirect_targets()
{
	typeset member

	for member in $OSLL_DS_MEMBERS
	do
		run_quiet "$LOCK_LINT" declare "ds_ops::$member" targets \
		    :locklint_usbser_warlock_dummy || return $?
	done

	run_quiet "$LOCK_LINT" declare rseq_step::s_func targets \
	    $OSLL_RSEQ_TARGETS
}

run_session()
{
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_USBSER_LL" || exit $?
	run_load "$OSLL_RSEQ_LL" || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?
	declare_indirect_targets || exit $?

	if [[ "$OSLL_ONE_MODE" == with ]]; then
		run_quiet "$LOCK_LINT" declare one usbser_state || exit $?
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

for database in usbser.ll usbser_rseq.ll
do
	if [[ -e "$database" ]]; then
		print -u2 "usbser module directory already contains $database"
		print -u2 "rename or remove it before starting"
		exit 1
	fi
done

mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/usbser.ll" \
    "$WORK_DIR/usbser_rseq.ll" "$WORK_DIR/check-kmod-usbser-adapter.ll"

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

print "Compiling usbser sources for OSLL"
compile_source ../../common/io/usb/clients/usbser/usbser.c usbser.ll &&
    compile_source ../../common/io/usb/clients/usbser/usbser_rseq.c \
    usbser_rseq.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-usbser-adapter.c" \
    check-kmod-usbser-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_USBSER_LL="$WORK_DIR/usbser.ll"
OSLL_RSEQ_LL="$WORK_DIR/usbser_rseq.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-usbser-adapter.ll"
if [[ ! -f "$OSLL_USBSER_LL" || ! -f "$OSLL_RSEQ_LL" ||
    ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_ONE_MODE OSLL_SESSION_MODE OSLL_USBSER_LL \
    OSLL_RSEQ_LL OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over usbser ($OSLL_ONE_MODE one declaration)"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL usbser session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL usbser session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

if [[ ! -f "$REFERENCE" ]]; then
	print "OSLL usbser baseline written to $OUTPUT."
	exit 0
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL usbser semantic output matches check-kmod-usbser.ref."
else
	print -u2 "OSLL usbser semantic output differs from " \
	    "check-kmod-usbser.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

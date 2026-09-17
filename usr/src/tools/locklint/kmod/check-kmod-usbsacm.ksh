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
# Run new locklint over the combined usbsacm, usbser, and recovery-sequence
# translation units using their production compiler arguments.  Require
# complete function and root coverage, every retained indirect call, exact
# diagnostic classes, and one sentinel before comparing all corrected OSLL
# problems with documented reporting normalizations.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbsacm"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbsacm.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbsacm.out"
USBSACM_SOURCE=../../common/io/usb/clients/usbser/usbsacm/usbsacm.c
USBSER_SOURCE=../../common/io/usb/clients/usbser/usbser.c
RSEQ_SOURCE=../../common/io/usb/clients/usbser/usbser_rseq.c

USBSACM_FUNCTIONS='
_init
_fini
_info
usbsacm_attach
usbsacm_detach
usbsacm_getinfo
usbsacm_open
usbsacm_ds_attach
usbsacm_ds_detach
usbsacm_ds_register_cb
usbsacm_ds_unregister_cb
usbsacm_ds_open_port
usbsacm_ds_close_port
usbsacm_ds_usb_power
usbsacm_ds_suspend
usbsacm_ds_resume
usbsacm_ds_disconnect
usbsacm_ds_reconnect
usbsacm_ds_set_port_params
usbsacm_ds_set_modem_ctl
usbsacm_ds_get_modem_ctl
usbsacm_ds_break_ctl
usbsacm_ds_tx
usbsacm_ds_rx
usbsacm_ds_stop
usbsacm_ds_start
usbsacm_ds_fifo_flush
usbsacm_ds_fifo_drain
usbsacm_fifo_flush_locked
usbsacm_get_bulk_pipe_number
usbsacm_init_ports_status
usbsacm_init_alloc_ports
usbsacm_free_ports
usbsacm_get_descriptors
usbsacm_cleanup
usbsacm_restore_device_state
usbsacm_restore_port_state
usbsacm_open_port_pipes
usbsacm_close_port_pipes
usbsacm_close_pipes
usbsacm_disconnect_pipes
usbsacm_reconnect_pipes
usbsacm_bulkin_cb
usbsacm_bulkout_cb
usbsacm_rx_start
usbsacm_tx_start
usbsacm_send_data
usbsacm_wait_tx_drain
usbsacm_req_write
usbsacm_set_line_coding
usbsacm_mctl2reg
usbsacm_reg2mctl
usbsacm_put_tail
usbsacm_put_head
usbsacm_create_pm_components
usbsacm_destroy_pm_components
usbsacm_pm_set_busy
usbsacm_pm_set_idle
usbsacm_pwrlvl0
usbsacm_pwrlvl1
usbsacm_pwrlvl2
usbsacm_pwrlvl3
usbsacm_pipe_start_polling
usbsacm_intr_cb
usbsacm_intr_ex_cb
usbsacm_parse_intr_data
'

USBSER_FUNCTIONS='
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

REQUIRED_AUTOMATIC_ROOTS='
usbser_first_device
usbser_putchar
usbser_getchar
usbser_ischar
usbser_polledio_enter
usbser_polledio_exit
usbser_soft_state_size
usbser_open
usbser_close
usbser_wput
usbser_wsrv
usbser_rsrv
usbser_tx_cb
usbser_rx_cb
usbser_status_cb
usbser_wq_thread
usbser_rq_thread
usbser_disconnect_cb
usbser_reconnect_cb
usbser_attach
usbser_detach
usbser_getinfo
usbser_power
usbser_restart
usbsacm_open
usbsacm_bulkin_cb
usbsacm_bulkout_cb
usbsacm_intr_cb
usbsacm_intr_ex_cb
usbsacm_attach
usbsacm_detach
usbsacm_getinfo
'

DS_CALL_LINES='
686
782
805
872
949
1032
1179
1260
1262
1518
1598
1639
1670
1793
1795
1798
1818
1837
2082
2360
2442
2571
2655
2682
2688
2703
2710
2728
2730
2732
2742
2753
2767
2893
2895
2897
2941
2960
3000
3013
3034
3084
3187
3194
3249
3252
3388
3395
'

RSEQ_CALL_LINES='
61
62
91
92
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbsacm module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi

mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$REFERENCE_FILTERED" "$OUTPUT"

run_locklint()
{
	"$LOCKLINT" "$@" \
	    -fident -finline -fno-inline-functions -fno-builtin -fno-asm \
	    -fdiagnostics-show-option -nodefaultlibs -D__sun -m64 \
	    -mtune=opteron -Ui386 -U__i386 -fno-strict-aliasing \
	    -fno-unit-at-a-time -fno-optimize-sibling-calls -O2 \
	    -D_ASM_INLINES -ffreestanding -mno-red-zone -mno-mmx -mno-sse \
	    -msave-args -Wall -Wextra -g -gdwarf-4 -gstrict-dwarf \
	    -std=gnu99 -msave-args -Werror \
	    -Wno-missing-braces -Wno-sign-compare -Wno-unused-parameter \
	    -Wno-missing-field-initializers -Winline \
	    -Wno-maybe-uninitialized -fno-inline-small-functions \
	    -fno-inline-functions-called-once -fno-ipa-cp -fno-ipa-icf \
	    -fno-clone-functions -fno-reorder-functions \
	    -fno-reorder-blocks-and-partition \
	    -fno-aggressive-loop-optimizations \
	    --param=max-inline-insns-single=450 -fno-shrink-wrap \
	    -mindirect-branch=thunk-extern -mindirect-branch-register \
	    -fno-asynchronous-unwind-tables -fstack-protector-strong \
	    -D_KERNEL -ffreestanding -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I../../intel -nostdinc -I../../common \
	    -mcmodel=kernel \
	    "$USBSACM_SOURCE" "$USBSER_SOURCE" "$RSEQ_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbsacm-sentinel.c"
}

verify_function_list()
{
	source=$1
	shift
	typeset function
	typeset missing=

	for function
	do
		if ! grep -q \
		    "^function ${function} tu=${source} .*reachable=yes\$" \
		    "$ANALYZE_RAW"; then
			missing="$missing $function"
		fi
	done
	if [[ -n "$missing" ]]; then
		print -u2 "new locklint function inventory omitted:$missing"
		return 1
	fi
}

verify_module_coverage()
{
	verify_function_list "$USBSACM_SOURCE" $USBSACM_FUNCTIONS ||
	    return 1

	typeset rseq_functions=
	typeset usbser_functions=
	typeset function

	for function in $USBSER_FUNCTIONS
	do
		case "$function" in
		rseq_*)
			rseq_functions="$rseq_functions $function"
			;;
		*)
			usbser_functions="$usbser_functions $function"
			;;
		esac
	done
	verify_function_list "$USBSER_SOURCE" $usbser_functions || return 1
	verify_function_list "$RSEQ_SOURCE" $rseq_functions
}

verify_automatic_roots()
{
	typeset function

	for function in $REQUIRED_AUTOMATIC_ROOTS
	do
		if ! awk -v target="$function" '
		    /^function / {
			in_function = ($2 == target)
		    }
		    in_function && /^  root / {
			found = 1
		    }
		    END {
			exit !found
		    }
		' "$ANALYZE_RAW"; then
			print -u2 "new locklint did not infer root $function"
			return 1
		fi
	done

	grep -q "^  call ${USBSER_SOURCE}:398:.* resolved usbser_cpr_resume " \
	    "$ANALYZE_RAW" || return 1
	grep -q "^  call ${USBSER_SOURCE}:455:.* resolved usbser_cpr_suspend " \
	    "$ANALYZE_RAW"
}

verify_indirect_calls()
{
	typeset line

	for line in $DS_CALL_LINES
	do
		grep -q "^  call ${USBSER_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW" || return 1
	done
	for line in $RSEQ_CALL_LINES
	do
		grep -q "^  call ${RSEQ_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW" || return 1
	done
	for line in 2286 2342
	do
		grep -q "^  call ${USBSACM_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW" || return 1
	done

	indirect_count=$(grep -c '^  call .* indirect$' "$ANALYZE_RAW")
	if (( indirect_count != 54 )); then
		print -u2 "new locklint retained $indirect_count indirect calls " \
		    "instead of 54"
		return 1
	fi
}

write_native_summary()
{
	awk -F "'" '
	    /(usbser|usbsacm)\.c:[0-9]+:[0-9]+: warning: locklint:/ &&
	    /\[unprotected-access\]$/ {
		match($0, /(usbser|usbsacm)\.c:[0-9]+/)
		location = substr($0, RSTART, RLENGTH)
		if (location == "usbsacm.c:1701" &&
		    $2 == "acm_ctrl_if_no")
			next
		print location, $2
	    }
	' "$ANALYZE_RAW" | LC_ALL=C sort
}

write_reference_summary()
{
	awk '
	    $0 !~ /^#/ {
		if ($1 == "usbser.c:848" && $2 == "port_lh")
			$1 = "usbser.c:846"
		else if ($1 == "usbser.c:944" && $2 == "port_state")
			next
		else if ($1 == "usbsacm.c:606" && $2 == "acm_lh")
			$1 = "usbsacm.c:605"
		else if ($1 == "usbsacm.c:1635" && $2 == "acm_ports")
			$1 = "usbsacm.c:1634"
		else if ($1 == "usbsacm.c:1752" && $2 == "acm_cap")
			$1 = "usbsacm.c:1751"
		else if ($1 == "usbsacm.c:2766" && $2 == "acm_pm")
			$1 = "usbsacm.c:2765"
		print
	    }
	' "$REFERENCE" | LC_ALL=C sort
}

print "Running new locklint over usbsacm and usbser"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbsacm.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbsacm analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1
verify_indirect_calls || exit 1

duplicate_count=$(grep -Ec \
    "warning: multiple definitions for function '_(init|fini|info)'" \
    "$ANALYZE_RAW")
if (( duplicate_count != 3 )); then
	print -u2 "new locklint reported $duplicate_count expected module-entry " \
	    "collisions instead of three"
	exit 1
fi

sentinel_count=$(grep -c \
    'check-kmod-usbsacm-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbsacm analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	exit 1
fi

usbsacm_unprotected=$(grep -c \
    'usbsacm.c:.*warning: locklint:.*\[unprotected-access\]$' "$ANALYZE_RAW")
usbsacm_conditional=$(grep -c \
    'usbsacm.c:.*warning: locklint:.*\[conditional-protection\]$' \
    "$ANALYZE_RAW")
usbser_unprotected=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[unprotected-access\]$' "$ANALYZE_RAW")
usbser_conditional=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[conditional-protection\]$' \
    "$ANALYZE_RAW")
usbser_asserted=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[asserted-lock-requirement\]$' \
    "$ANALYZE_RAW")
if (( usbsacm_unprotected != 37 || usbsacm_conditional != 3 ||
    usbser_unprotected != 15 || usbser_conditional != 30 ||
    usbser_asserted != 7 )); then
	print -u2 "Unexpected combined usbsacm diagnostic counts:"
	print -u2 "  usbsacm unprotected=$usbsacm_unprotected (expected 37)"
	print -u2 "  usbsacm conditional=$usbsacm_conditional (expected 3)"
	print -u2 "  usbser unprotected=$usbser_unprotected (expected 15)"
	print -u2 "  usbser conditional=$usbser_conditional (expected 30)"
	print -u2 "  usbser asserted=$usbser_asserted (expected 7)"
	exit 1
fi

for exception in \
    'usbser.c:848 port_lh' \
    'usbser.c:944 port_state' \
    'usbsacm.c:606 acm_lh' \
    'usbsacm.c:1635 acm_ports' \
    'usbsacm.c:1752 acm_cap' \
    'usbsacm.c:2766 acm_pm'
do
	grep -q "^${exception}\$" "$REFERENCE" || {
		print -u2 "OSLL reference lacks reporting exception: $exception"
		exit 1
	}
done
grep -q \
    'usbsacm.c:1701:.*protected member.*acm_ctrl_if_no.*\[unprotected-access\]$' \
    "$ANALYZE_RAW" || {
	print -u2 "native usbsacm result lacks the line-1701 extra store"
	exit 1
}

write_native_summary >"$OUTPUT" || exit 1
write_reference_summary >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbsacm covers every corrected OSLL problem" \
	    "with documented reporting exceptions."
else
	print -u2 "New locklint usbsacm problem set differs from reference:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

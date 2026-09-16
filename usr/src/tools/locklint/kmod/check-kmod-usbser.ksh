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
# Run new locklint over both standalone usbser translation units using their
# recorded kernel compiler arguments.  Require complete function coverage,
# all historical roots, every retained indirect call, and one deliberate
# lock diagnostic.  Normalize the two accepted reporting differences before
# comparing exact source/member pairs with corrected OSLL.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbser"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbser.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbser.out"
USBSER_SOURCE=../../common/io/usb/clients/usbser/usbser.c
RSEQ_SOURCE=../../common/io/usb/clients/usbser/usbser_rseq.c

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

HISTORICAL_AUTOMATIC_ROOTS='
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
	print -u2 "usbser module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi

mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$REFERENCE_FILTERED" "$OUTPUT" \
    "$WORK_DIR/native.comparable"

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
	    -fno-inline-small-functions \
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
	    "$USBSER_SOURCE" "$RSEQ_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbser-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USBSER_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} .*usbser\\(_rseq\\)\\{0,1\\}.c:.*reachable=yes\$" \
		    "$ANALYZE_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "new locklint function inventory omitted:$missing"
		return 1
	fi
}

verify_historical_coverage()
{
	typeset function

	for function in $HISTORICAL_AUTOMATIC_ROOTS
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
			print -u2 \
			    "new locklint did not infer historical root $function"
			return 1
		fi
	done

	if ! grep -q \
	    "^  call ${USBSER_SOURCE}:398:.* resolved usbser_cpr_resume " \
	    "$ANALYZE_RAW"; then
		print -u2 "new locklint did not resolve usbser_attach to " \
		    "historical root usbser_cpr_resume"
		return 1
	fi
	if ! grep -q \
	    "^  call ${USBSER_SOURCE}:455:.* resolved usbser_cpr_suspend " \
	    "$ANALYZE_RAW"; then
		print -u2 "new locklint did not resolve usbser_detach to " \
		    "historical root usbser_cpr_suspend"
		return 1
	fi
}

verify_indirect_calls()
{
	typeset line

	for line in $DS_CALL_LINES
	do
		if ! grep -q \
		    "^  call ${USBSER_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW"; then
			print -u2 "new locklint did not retain indirect ds_ops " \
			    "call at usbser.c:$line"
			return 1
		fi
	done

	for line in $RSEQ_CALL_LINES
	do
		if ! grep -q \
		    "^  call ${RSEQ_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW"; then
			print -u2 "new locklint did not retain indirect recovery " \
			    "call at usbser_rseq.c:$line"
			return 1
		fi
	done

	indirect_count=$(grep -c '^  call .* indirect$' "$ANALYZE_RAW")
	if (( indirect_count != 52 )); then
		print -u2 "new locklint retained $indirect_count indirect calls " \
		    "instead of 52"
		return 1
	fi
}

write_native_summary()
{
	awk -F "'" '
	    /usbser.c:[0-9]+:[0-9]+: warning: locklint:/ &&
	    /\[unprotected-access\]$/ {
		match($0, /usbser.c:[0-9]+/)
		location = substr($0, RSTART, RLENGTH)
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
		print
	    }
	' "$REFERENCE" | LC_ALL=C sort
}

print "Running new locklint over usbser"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbser.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbser analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_historical_coverage || exit 1
verify_indirect_calls || exit 1

sentinel_count=$(grep -c \
    'check-kmod-usbser-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbser analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

unprotected_count=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
conditional_count=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[conditional-protection\]$' \
    "$ANALYZE_RAW")
asserted_count=$(grep -c \
    'usbser.c:.*warning: locklint:.*\[asserted-lock-requirement\]$' \
    "$ANALYZE_RAW")
if (( unprotected_count != 15 || conditional_count != 30 ||
    asserted_count != 7 )); then
	print -u2 "Unexpected new locklint usbser diagnostic counts:"
	print -u2 "  unprotected=$unprotected_count (expected 15)"
	print -u2 "  conditional=$conditional_count (expected 30)"
	print -u2 "  asserted=$asserted_count (expected 7)"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

if ! grep -q '^usbser.c:848 port_lh$' "$REFERENCE" ||
    ! grep -q '^usbser.c:944 port_state$' "$REFERENCE"; then
	print -u2 "OSLL usbser reference lacks expected reporting exceptions"
	exit 1
fi

write_native_summary >"$OUTPUT" || exit 1
write_reference_summary >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbser covers every corrected OSLL problem" \
	    "with documented reporting exceptions."
else
	print -u2 "New locklint usbser problem set differs from " \
	    "check-kmod-usbser.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

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
# Run new locklint over the complete standalone usbecm kernel module using
# its recorded cw arguments.  Require complete function coverage, automatic
# historical roots, the five unassigned device-specific calls, and one
# deliberate lock diagnostic before comparing protected-member multiplicities
# with OSLL.  Two explicitly checked compiler-lowering differences are
# excluded; source anchors and emission order are intentionally excluded.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbecm"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbecm.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbecm.out"
USBECM_SOURCE=../../common/io/usb/clients/usbecm/usbecm.c
USBECM_FUNCTIONS='
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
HISTORICAL_AUTOMATIC_ROOTS='
usbecm_bulkin_cb
usbecm_bulkout_cb
usbecm_intr_cb
usbecm_intr_ex_cb
usbecm_power
usbecm_m_stop
usbecm_m_start
usbecm_m_unicst
usbecm_m_multicst
usbecm_m_promisc
usbecm_m_ioctl
usbecm_m_tx
usbecm_m_getprop
usbecm_m_setprop
usbecm_m_stat
usbecm_disconnect_event_cb
usbecm_reconnect_event_cb
'
DS_CALL_LINES='
289
377
676
721
1652
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbecm module directory not found: $MODULE_DIR"
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
	    "$USBECM_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbecm-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USBECM_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} .*usbecm.c:.*reachable=yes\$" \
		    "$ANALYZE_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "new locklint function inventory omitted:$missing"
		return 1
	fi
}

verify_automatic_roots()
{
	typeset function

	for function in $HISTORICAL_AUTOMATIC_ROOTS
	do
		if ! awk -v target="$function" '
		    /^function / {
			in_function = ($2 == target)
		    }
		    in_function && /^  root function-pointer-escape$/ {
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
}

verify_unresolved_ds_calls()
{
	typeset line

	for line in $DS_CALL_LINES
	do
		if ! grep -q \
		    "^  call ${USBECM_SOURCE}:${line}:.* indirect\$" \
		    "$ANALYZE_RAW"; then
			print -u2 "new locklint did not retain unresolved " \
			    "usbecm_ds_ops call at usbecm.c:$line"
			return 1
		fi
	done
}

write_summary()
{
	awk -F "'" '
	    /warning: locklint:/ &&
	    /\[unprotected-access\]$/ &&
	    $0 !~ /check-kmod-usbecm-sentinel.c:/ {
		print $2
	    }
	' "$ANALYZE_RAW" | LC_ALL=C sort
}

print "Running new locklint over usbecm"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbecm.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbecm analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1
verify_unresolved_ds_calls || exit 1

sentinel_count=$(grep -c \
    'check-kmod-usbecm-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbecm analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

finding_count=$(grep -c \
    'usbecm.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( finding_count != 41 )); then
	print -u2 "new locklint usbecm analysis produced $finding_count " \
	    "protected-access diagnostics instead of 41"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

assertion_count=$(grep -c '\[assertion-requirement\]$' "$ANALYZE_RAW")
if (( assertion_count != 0 )); then
	print -u2 "new locklint usbecm analysis produced $assertion_count " \
	    "assertion-requirement diagnostics instead of none"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

write_summary >"$OUTPUT" || exit 1
awk '$0 !~ /^#/ { print $2 }' "$REFERENCE" | LC_ALL=C sort \
    >"$REFERENCE_FILTERED" || exit 1

native_ctrl_count=$(grep -c '^ecm_ctrl_if_no$' "$OUTPUT")
native_flags_count=$(grep -c '^ecm_init_flags$' "$OUTPUT")
osll_ctrl_count=$(grep -c '^ecm_ctrl_if_no$' "$REFERENCE_FILTERED")
osll_flags_count=$(grep -c '^ecm_init_flags$' "$REFERENCE_FILTERED")
if (( native_ctrl_count != 3 || osll_ctrl_count != 2 ||
    native_flags_count != 11 || osll_flags_count != 12 )); then
	print -u2 "Unexpected usbecm compiler-lowering counts:"
	print -u2 "  native ecm_ctrl_if_no=$native_ctrl_count (expected 3)"
	print -u2 "  OSLL ecm_ctrl_if_no=$osll_ctrl_count (expected 2)"
	print -u2 "  native ecm_init_flags=$native_flags_count (expected 11)"
	print -u2 "  OSLL ecm_init_flags=$osll_flags_count (expected 12)"
	exit 1
fi

grep -v -E '^(ecm_ctrl_if_no|ecm_init_flags)$' "$OUTPUT" \
    >"$WORK_DIR/native.comparable" || exit 1
grep -v -E '^(ecm_ctrl_if_no|ecm_init_flags)$' "$REFERENCE_FILTERED" \
    >"$WORK_DIR/reference.comparable" || exit 1

if cmp -s "$WORK_DIR/reference.comparable" "$WORK_DIR/native.comparable"; then
	rm -f "$OUTPUT"
	print "New locklint usbecm protected-member multiplicities match" \
	    "check-kmod-usbecm.ref with documented lowering exceptions."
else
	print -u2 "New locklint usbecm comparable protected-member " \
	    "multiplicities differ from check-kmod-usbecm.ref:"
	diff -u "$WORK_DIR/reference.comparable" \
	    "$WORK_DIR/native.comparable"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

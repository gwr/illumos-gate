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
# Run new locklint over the complete standalone usbprn kernel module using
# its recorded cw arguments.  Require complete function coverage, automatic
# historical roots, explicit modeled physio callback edges, and one deliberate
# lock diagnostic before comparing the complete protected-member multiset
# with OSLL.  Source anchors and emission order are intentionally excluded.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbprn"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbprn.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbprn.out"
USBPRN_SOURCE=../../common/io/usb/clients/printer/usbprn.c
USBPRN_FUNCTIONS='
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
HISTORICAL_ROOTS='
usbprn_open
usbprn_close
usbprn_ioctl
usbprn_bulk_xfer_cb
usbprn_bulk_xfer_exc_cb
usbprn_reconnect_event_cb
usbprn_disconnect_event_cb
usbprn_power
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbprn module directory not found: $MODULE_DIR"
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
	    "$USBPRN_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbprn-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USBPRN_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} .*usbprn.c:.*reachable=yes\$" \
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

	for function in $HISTORICAL_ROOTS
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

verify_physio_edges()
{
	typeset function

	for function in usbprn_strategy usbprn_minphys
	do
		if ! grep -q \
		    " resolved ${function} .*usbprn.c" \
		    "$ANALYZE_RAW"; then
			print -u2 \
			    "new locklint did not model physio callback $function"
			return 1
		fi
	done
}

write_summary()
{
	awk -F "'" '
	    /warning: locklint:/ &&
	    /\[unprotected-access\]$/ &&
	    $0 !~ /check-kmod-usbprn-sentinel.c:/ {
		print $2
	    }
	' "$ANALYZE_RAW" | LC_ALL=C sort
}

print "Running new locklint over usbprn"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbprn.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbprn analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1
verify_physio_edges || exit 1

sentinel_count=$(grep -c \
    'check-kmod-usbprn-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbprn analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

finding_count=$(grep -c \
    'usbprn.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( finding_count != 51 )); then
	print -u2 "new locklint usbprn analysis produced $finding_count " \
	    "protected-access diagnostics instead of 51"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

assertion_count=$(grep -c '\[assertion-requirement\]$' "$ANALYZE_RAW")
if (( assertion_count != 0 )); then
	print -u2 "new locklint usbprn analysis produced $assertion_count " \
	    "assertion-requirement diagnostics instead of none"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

write_summary >"$OUTPUT" || exit 1
awk '$0 !~ /^#/ { print $2 }' "$REFERENCE" | LC_ALL=C sort \
    >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbprn protected-member multiset matches" \
	    "check-kmod-usbprn.ref."
else
	print -u2 "New locklint usbprn protected-member multiset differs " \
	    "from check-kmod-usbprn.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

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
# its recorded cw arguments.  Apply the analysis-only source patch to a copied
# input, then add the common DDI/DKI representative model.  Require complete
# function coverage, automatic historical roots, modeled operation-vector
# entries, exact callback actuals, the accepted diagnostic multiset, and the
# accepted native protection inventory.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
REPO_ROOT=$(CDPATH= cd "$SRC/../.." && pwd)
MODULE_DIR="$SRC/uts/intel/usbprn"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
DIAGNOSTIC_REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbprn.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbprn.out"
PATCH="$SCRIPT_DIR/check-kmod-usbprn.patch"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
PROTECTION_REFERENCE="$SCRIPT_DIR/check-kmod-usbprn.ref"
PROTECTION_RAW="$WORK_DIR/protection.raw"
PROTECTION_FILTERED="$WORK_DIR/protection.filtered"
SENTINEL="$SCRIPT_DIR/check-kmod-sentinel.c"
USBPRN_SOURCE=../../common/io/usb/clients/printer/usbprn.c
ANALYSIS_SOURCE="$WORK_DIR/usbprn.c"
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
if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi
for file in "$PATCH" "$MODEL" "$PROTECTION_REFERENCE" "$SENTINEL"
do
	if [[ ! -f "$file" ]]; then
		print -u2 "usbprn comparison input not found: $file"
		exit 1
	fi
done

cd "$MODULE_DIR" || exit 1
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$REFERENCE_FILTERED" "$OUTPUT" "$PROTECTION_RAW" \
    "$PROTECTION_FILTERED"
cp "$USBPRN_SOURCE" "$ANALYSIS_SOURCE" || exit 1
(
	cd "$WORK_DIR" || exit 1
	patch -s -p9 < "$PATCH"
) || exit 1

run_locklint()
{
	typeset sentinel=$1
	shift

	if [[ -n "$sentinel" ]]; then
		set -- "$@" "$sentinel"
	fi
	"$LOCKLINT" "$@" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I../../intel -nostdinc -I../../common \
	    "$ANALYSIS_SOURCE" \
	    "$MODEL"
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

verify_physio_model()
{
	typeset function

	if ! grep -q \
	    "^  call .*usbprn.c:.* resolved physio tu=$MODEL\$" \
	    "$ANALYZE_RAW"; then
		print -u2 "new locklint did not resolve the physio model"
		return 1
	fi

	for function in usbprn_strategy usbprn_minphys
	do
		if ! grep -q \
		    "^  escape .*usbprn.c:.* ${function} resolved ${function} tu=.*usbprn.c\$" \
		    "$ANALYZE_RAW"; then
			print -u2 \
			    "new locklint did not retain exact physio callback " \
			    "$function"
			return 1
		fi
	done

	if grep -q \
	    "$MODEL:.*\\[unmodeled-indirect-call\\]\$" "$ANALYZE_RAW"; then
		print -u2 \
		    "new locklint did not propagate the physio callback targets"
		return 1
	fi
}

verify_entry_model()
{
	typeset function
	typeset detach_pattern
	typeset no_entry_count
	typeset pattern
	typeset target

	for function in usbprn_info usbprn_attach usbprn_power \
	    usbprn_open usbprn_close usbprn_read usbprn_write usbprn_ioctl \
	    usbprn_poll
	do
		pattern="^  call $MODEL:.* resolved-indirect ${function} "
		pattern="${pattern}tu=.*usbprn.c\$"
		if ! grep -q "$pattern" "$ANALYZE_RAW"; then
			print -u2 \
			    "common DDI/DKI model did not call $function"
			return 1
		fi
	done

	for target in locklint_no_dev locklint_no_reset locklint_no_strategy \
	    locklint_no_print locklint_no_dump locklint_no_devmap \
	    locklint_no_mmap locklint_no_segmap locklint_no_prop_op \
	    locklint_no_aio
	do
		pattern="^  call $MODEL:.* resolved-indirect ${target} "
		if ! grep -q "$pattern" "$ANALYZE_RAW"; then
			print -u2 \
			    "common DDI/DKI model did not call $target"
			return 1
		fi
	done

	no_entry_count=$(grep -Ec \
	    "^  call $MODEL:.* resolved-indirect locklint_no_(dev|reset|strategy|print|dump|devmap|mmap|segmap|prop_op|aio) " \
	    "$ANALYZE_RAW")
	if (( no_entry_count != 13 )); then
		print -u2 "common DDI/DKI model leaf fallback count changed"
		return 1
	fi

	detach_pattern="^  call $MODEL:.* resolved-indirect usbprn_detach "
	detach_pattern="${detach_pattern}tu=.*usbprn.c\$"
	if ! grep -q \
	    "^  call .*usbprn.c:.* resolved mod_remove tu=$MODEL\$" \
	    "$ANALYZE_RAW" ||
	    ! grep -q "$detach_pattern" "$ANALYZE_RAW"; then
		print -u2 "common DDI/DKI model did not represent module detach"
		return 1
	fi
}

write_summary()
{
	awk -F "'" '
	    /warning:/ &&
	    /\[unprotected-access\]$/ &&
	    $0 !~ /check-kmod-sentinel.c:/ {
		print $2
	    }
	' "$ANALYZE_RAW" | LC_ALL=C sort
}

print "Running new locklint over usbprn"
run_locklint "$SENTINEL" --compat=osll --dump-callgraph \
    --cf "$SCRIPT_DIR/usbprn.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbprn analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1
verify_physio_model || exit 1
verify_entry_model || exit 1

sentinel_count=$(grep -c \
    'check-kmod-sentinel.c:.*warning:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbprn analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

finding_count=$(grep -c \
    'usbprn.c:.*warning:.*\[unprotected-access\]$' \
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
awk '$0 !~ /^#/ { print $2 }' "$DIAGNOSTIC_REFERENCE" | LC_ALL=C sort \
    >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbprn protected-member multiset matches" \
	    "the OSLL diagnostic reference."
else
	print -u2 "New locklint usbprn protected-member multiset differs " \
	    "from check-kmod-usbprn.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

run_locklint "" --compat=osll --no-diagnostics \
    --cf "$SCRIPT_DIR/usbprn.cf" \
    --audit-protection="$PROTECTION_RAW" >/dev/null 2>&1 || exit 1
awk -v copied="$ANALYSIS_SOURCE::" -v source="$USBPRN_SOURCE::" '
    /^locklint_(bus|cb|dev)_ops([.\t]|$)/ {
	next
    }
    {
	sub(copied, source)
	print
    }
' "$PROTECTION_RAW" >"$PROTECTION_FILTERED" || exit 1
if cmp -s "$PROTECTION_REFERENCE" "$PROTECTION_FILTERED"; then
	print "New locklint usbprn protection audit matches the native reference."
else
	print -u2 "New locklint usbprn protection audit differs from " \
	    "check-kmod-usbprn.ref:"
	diff -u "$PROTECTION_REFERENCE" "$PROTECTION_FILTERED"
	exit 1
fi

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
# Run new locklint over the complete usbskel kernel module using the frontend
# arguments recorded in its cw command.  Require complete function coverage,
# automatic discovery of the historical callback roots, and one deliberate
# lock diagnostic before comparing usbskel's findings with OSLL.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbskel"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbskel.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbskel.out"
USBSKEL_SOURCE=../../common/io/usb/clients/usbskel/usbskel.c
USBSKEL_FUNCTIONS='
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
HISTORICAL_ROOTS='
usbskel_normal_callback
usbskel_exception_callback
usbskel_disconnect_callback
usbskel_reconnect_callback
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbskel module directory not found: $MODULE_DIR"
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
	    "$USBSKEL_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbskel-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USBSKEL_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} tu=${USBSKEL_SOURCE} .*reachable=yes\$" \
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

print "Running new locklint over usbskel"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbskel.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbskel analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1

sentinel_count=$(grep -c \
    'check-kmod-usbskel-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbskel analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

awk '/warning: locklint:/ && $0 !~ /check-kmod-usbskel-sentinel.c:/ { print }' \
    "$ANALYZE_RAW" >"$OUTPUT" || exit 1
awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbskel semantic output matches " \
	    "check-kmod-usbskel.ref."
else
	print -u2 "New locklint usbskel semantic output differs from " \
	    "check-kmod-usbskel.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

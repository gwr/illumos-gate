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
# Run new locklint over the complete standalone usb_ah kernel module using
# its recorded production compiler arguments.  Require complete function
# coverage, all five historical roots, no unresolved indirect calls, and one
# deliberate lock diagnostic before comparing usb_ah's empty result with OSLL.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usb_ah"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usb-ah.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usb-ah.out"
USB_AH_SOURCE=../../common/io/usb/clients/audio/usb_ah/usb_ah.c

USB_AH_FUNCTIONS='
_init
_fini
_info
usb_ah_open
usb_ah_close
usb_ah_wput
usb_ah_rput
usb_ah_mctl_receive
usb_ah_repeat_send
usb_ah_timeout
usb_ah_cancel_timeout
usb_ah_cp_mblk
usb_ah_get_cooked_rd
usb_ah_check_usage_send_data
usb_ah_mk_mctl
'

HISTORICAL_ROOTS='
usb_ah_open
usb_ah_close
usb_ah_wput
usb_ah_rput
usb_ah_timeout
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usb_ah module directory not found: $MODULE_DIR"
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
	    "$USB_AH_SOURCE" "$SCRIPT_DIR/check-kmod-usb-ah-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USB_AH_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} tu=${USB_AH_SOURCE} .*reachable=yes\$" \
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

print "Running new locklint over usb_ah"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usb_ah.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usb_ah analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1

indirect_count=$(grep -c '^  call .* indirect$' "$ANALYZE_RAW")
if (( indirect_count != 0 )); then
	print -u2 "new locklint usb_ah retained $indirect_count indirect " \
	    "calls instead of none"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

sentinel_count=$(grep -c \
    'check-kmod-usb-ah-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usb_ah analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

awk '/warning: locklint:/ && $0 !~ /check-kmod-usb-ah-sentinel.c:/ {
	print
}' "$ANALYZE_RAW" >"$OUTPUT" || exit 1
awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usb_ah semantic output matches " \
	    "check-kmod-usb-ah.ref."
else
	print -u2 "New locklint usb_ah semantic output differs from " \
	    "check-kmod-usb-ah.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

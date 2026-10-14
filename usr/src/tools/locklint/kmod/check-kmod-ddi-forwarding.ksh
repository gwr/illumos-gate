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
# Validate current direct DMA, property, and event forwarding in the common
# DDI/DKI representative model.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
INPUT="$SCRIPT_DIR/check-kmod-ddi-forwarding.c"
OUTPUT="$WORK_DIR/ddi-forwarding.raw"

mkdir -p "$WORK_DIR" || exit 1
rm -f "$OUTPUT"

cd "$MODULE_DIR" || exit 1
"$LOCKLINT" --compat=osll --no-diagnostics --dump-callgraph \
    --cf "$SCRIPT_DIR/ddi-forwarding.cf" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -I../../intel -nostdinc -I../../common \
    "$INPUT" "$MODEL" >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "DDI forwarding model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

verify_forwarding()
{
	typeset function=$1
	typeset target=$2

	if ! grep -q \
	    "^  call $INPUT:.* resolved ${function} tu=$MODEL\$" "$OUTPUT"; then
		print -u2 "DDI forwarding input did not resolve $function"
		return 1
	fi

	if ! awk -v fn="$function" -v target="$target" '
	    /^function / {
		in_function = ($2 == fn)
	    }
	    in_function &&
	    $0 ~ "^  call .* resolved-indirect " target " " {
		found = 1
	    }
	    END {
		exit !found
	    }
	' "$OUTPUT"; then
		print -u2 "$function did not forward to $target"
		return 1
	fi
}

verify_forwarding ddi_dma_mctl locklint_no_bus_dma_ctl || exit 1
verify_forwarding ddi_dma_sync locklint_no_bus_dma_flush || exit 1
verify_forwarding ddi_prop_op locklint_no_bus_prop || exit 1
verify_forwarding ddi_get_eventcookie locklint_no_bus_get_event || exit 1
verify_forwarding ddi_add_event_handler locklint_no_bus_add_event || exit 1
verify_forwarding ddi_remove_event_handler locklint_no_bus_remove_event ||
    exit 1
verify_forwarding pm_set_power locklint_no_power || exit 1
verify_forwarding pm_raise_power locklint_no_power || exit 1
verify_forwarding pm_lower_power locklint_no_power || exit 1
verify_forwarding ndi_devi_online locklint_no_bus_config || exit 1
verify_forwarding ndi_devi_offline locklint_no_bus_unconfig || exit 1

print "New locklint DDI framework forwarding resolves."

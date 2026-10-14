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
# Validate the common DDI/DKI bus_ops representative model with pseudonex.
# Require every current bus operation to resolve to either a concrete
# pseudonex entry or a signature-correct analysis-only no-op target.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
OUTPUT="$WORK_DIR/pseudonex-model.raw"
SOURCE=../../common/io/pseudonex.c
PSEUDONEX_FUNCTIONS='
_init
_fini
_info
pseudonex_attach
pseudonex_detach
pseudonex_open
pseudonex_close
pseudonex_ioctl
pseudonex_intr_op
pseudonex_check_assignment
pseudonex_auto_assign
pseudonex_fm_init
pseudonex_ctl
'
BUS_TARGETS='
locklint_no_bus_map
locklint_no_bus_map_fault
locklint_no_bus_dma_allochdl
locklint_no_bus_dma_handle
locklint_no_bus_dma_bindhdl
locklint_no_bus_dma_flush
locklint_no_bus_dma_win
locklint_no_bus_dma_ctl
pseudonex_ctl
locklint_no_bus_prop
locklint_no_bus_get_event
locklint_no_bus_add_event
locklint_no_bus_remove_event
locklint_no_bus_post_event
locklint_no_bus_config
locklint_no_bus_unconfig
pseudonex_fm_init
locklint_no_bus_fm_fini
locklint_no_bus_fm_access
locklint_no_bus_power
pseudonex_intr_op
locklint_no_bus_hp
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "pseudonex module directory not found: $MODULE_DIR"
	exit 1
fi
if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi

mkdir -p "$WORK_DIR" || exit 1
rm -f "$OUTPUT"

cd "$MODULE_DIR" || exit 1
"$LOCKLINT" --compat=osll --no-diagnostics --dump-callgraph \
    --cf "$SCRIPT_DIR/pseudonex.cf" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -I../../intel -nostdinc -I../../common \
    "$SOURCE" "$MODEL" >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint pseudonex model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

for function in $PSEUDONEX_FUNCTIONS
do
	if ! grep -q \
	    "^function ${function} .*pseudonex.c:.*reachable=yes\$" "$OUTPUT"; then
		print -u2 "pseudonex model omitted $function"
		exit 1
	fi
done

for target in $BUS_TARGETS
do
	if ! grep -q \
	    "^  call $MODEL:.* resolved-indirect ${target} " "$OUTPUT"; then
		print -u2 "common DDI/DKI bus model did not call $target"
		exit 1
	fi
done

typeset no_entry_count
no_entry_count=$(awk '
    /^function / {
	in_function = ($2 == "locklint_ddi_dki_bus_model")
    }
    in_function &&
    /^  call .* resolved-indirect locklint_no_bus_/ {
	count++
    }
    END {
	print count + 0
    }
' "$OUTPUT")
if (( no_entry_count != 21 )); then
	print -u2 "common DDI/DKI bus fallback count changed"
	exit 1
fi

if ! grep -q \
    "^  call .*pseudonex.c:.* resolved mod_remove tu=$MODEL\$" "$OUTPUT" ||
    ! grep -q \
    "^  call $MODEL:.* resolved-indirect pseudonex_detach " "$OUTPUT"; then
	print -u2 "common DDI/DKI model did not represent pseudonex detach"
	exit 1
fi

print "New locklint pseudonex bus operation model resolves every current entry."

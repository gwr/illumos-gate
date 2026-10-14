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
# Validate DDI asynchronous registration against the shared representative
# model.  Exact callbacks must be independent roots and must not inherit the
# mutex held by the registering caller.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/ddi-dki-model.c"
INPUT="$SCRIPT_DIR/check-kmod-ddi-callbacks.c"
OUTPUT="$WORK_DIR/ddi-callbacks.raw"
REGISTRATIONS='
timeout
ddi_intr_add_handler
ddi_intr_add_softint
ddi_add_intr
ddi_add_softintr
ddi_set_callback
ddi_dma_alloc_handle
'
CALLBACKS='
timeout_callback
current_intr_callback
legacy_intr_callback
deferred_callback
dma_callback
'

mkdir -p "$WORK_DIR" || exit 1
rm -f "$OUTPUT"

cd "$MODULE_DIR" || exit 1
"$LOCKLINT" --compat=osll --dump-callgraph \
    --cf "$SCRIPT_DIR/ddi-callbacks.cf" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -I../../intel -nostdinc -I../../common \
    "$INPUT" "$MODEL" >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "DDI callback model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

for function in $REGISTRATIONS
do
	if ! grep -q \
	    "^  call $INPUT:.* resolved ${function} tu=$MODEL\$" "$OUTPUT"; then
		print -u2 "DDI callback model did not resolve $function"
		exit 1
	fi
done

for function in $CALLBACKS
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
	' "$OUTPUT"; then
		print -u2 "$function is not an independent callback root"
		exit 1
	fi
done

callback_warnings=$(grep -c \
    "check-kmod-ddi-callbacks.c:.*'callback_data'.*\\[unprotected-access\\]\$" \
    "$OUTPUT")
if (( callback_warnings != 10 )); then
	print -u2 "DDI callbacks produced $callback_warnings independent-state " \
	    "diagnostics instead of ten"
	print -u2 "see $OUTPUT"
	exit 1
fi

print "New locklint DDI callback registrations use independent callback roots."

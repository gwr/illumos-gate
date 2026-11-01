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
# Validate qwriter dispatch from unchanged SPPP source through one configured
# single-module STREAMS model.  The runner owns the current internal model
# configuration; these -D options are not an end-user interface.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/sppp"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
SPPP="$SRC/uts/common/io/ppp/sppp/sppp.c"
SPPP_DLPI="$SRC/uts/common/io/ppp/sppp/sppp_dlpi.c"
COMMANDS="$SCRIPT_DIR/sppp-qwriter.cf"
OUTPUT="$SCRIPT_DIR/sppp-qwriter.raw"

rm -f "$OUTPUT"
(
	cd "$MODULE_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none \
	    --dump-annotations --dump-callgraph \
	    --cf "$COMMANDS" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -DINTERNAL_BUILD -DSOL2 \
	    -DMODEL_PREFIX=locklint_sppp_ \
	    '-DMODEL_FLAGS=(D_MP|D_MTQPAIR|D_MTOUTPERIM|D_MTOCEXCL|D_MTPUTSHARED)' \
	    -DMODEL_QWRITER_QPAIR_SIDE=1 \
	    -I../../intel -nostdinc -I../../common \
	    -I../../common/io/ppp/common \
	    "$SPPP" "$SPPP_DLPI" "$MODEL"
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "SPPP qwriter model analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

calls=$(grep -c \
    "^  call .*sppp.*\\.c:.* resolved qwriter tu=$MODEL\$" "$OUTPUT")
annotations=$(grep -c \
    "ASYNC_ENTRY locklint_sppp_qwriter_deferred" "$OUTPUT")
unmodeled=$(grep -c \
    "indirect call through 'func'.*\\[unmodeled-indirect-call\\]$" \
    "$OUTPUT")
targets=0
for target in sppp_inner_ioctl sppp_outer_ioctl sppp_dl_attach_upper \
    sppp_dl_detach_upper sppp_dl_bind sppp_dl_unbind \
    sppp_dl_promiscon sppp_dl_promiscoff
do
	if grep -q "^function $target " "$OUTPUT"; then
		targets=$((targets + 1))
	fi
done

if (( calls != 8 || annotations != 1 || unmodeled != 0 ||
    targets != 8 )); then
	print -u2 "Unexpected SPPP qwriter model findings:"
	print -u2 "  resolved qwriter calls=$calls (expected 8)"
	print -u2 "  async qwriter annotations=$annotations (expected 1)"
	print -u2 "  unmodeled callback calls=$unmodeled (expected 0)"
	print -u2 "  callback definitions=$targets (expected 8)"
	print -u2 "see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint SPPP qwriter calls use bound asynchronous entry."

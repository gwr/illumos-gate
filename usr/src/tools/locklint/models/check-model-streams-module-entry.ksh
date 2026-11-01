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
# Validate exact read- and write-side module callback entry under one
# D_MTQPAIR perimeter.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/pseudo"
LOCKLINT="$SRC/tools/locklint/locklint"
MODEL="$SCRIPT_DIR/streams-dki-model.c"
INPUT="$SCRIPT_DIR/check-model-streams-module-entry.c"
COMMANDS="$SCRIPT_DIR/streams-module-entry.cf"
OUTPUT="$SCRIPT_DIR/streams-module-entry.raw"

rm -f "$OUTPUT"
(
	cd "$MODULE_DIR" || exit 1
	"$LOCKLINT" --compat=osll --root-discovery=none --dump-callgraph \
	    --cf "$COMMANDS" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -DMODEL_MODULE_ENTRY \
	    '-DMODEL_FLAGS=(D_MP|D_MTQPAIR)' \
	    -I../../intel -nostdinc -I../../common \
	    "$INPUT" "$MODEL"
) >"$OUTPUT" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "STREAMS exact module entry analysis failed with status $status"
	print -u2 "see $OUTPUT"
	exit "$status"
fi

for callback in \
    streams_module_read_put streams_module_write_put \
    streams_module_read_service streams_module_write_service \
    streams_module_read_open streams_module_write_open \
    streams_module_read_close streams_module_write_close
do
	count=$(grep -c " resolved-indirect $callback tu=$INPUT\$" "$OUTPUT")
	if (( count != 1 )); then
		print -u2 "STREAMS exact module entry resolved $callback " \
		    "$count times instead of once"
		print -u2 "see $OUTPUT"
		exit 1
	fi
done

warnings=$(grep -c "warning:" "$OUTPUT")
if (( warnings != 0 )); then
	print -u2 "STREAMS exact module entry produced $warnings warnings"
	print -u2 "see $OUTPUT"
	exit 1
fi

rm -f "$OUTPUT"
print "New locklint exact STREAMS module entry works."

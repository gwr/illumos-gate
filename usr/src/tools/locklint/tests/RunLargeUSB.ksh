#!/bin/ksh
#
# Run native locklint over the large usbprn-with-USBA integration workload.
# This workload is intentionally separate from the focused tests run by
# "make test".  Set DUMPS=1 to include development dumps and timing.
# SRC, LOCKLINT, CF, and OUT may override the defaults.
#

TESTDIR=$(cd "$(dirname "$0")" && pwd) || exit 1
if [[ -z "$SRC" ]]; then
	SRC=$(cd "$TESTDIR/../../.." && pwd) || exit 1
fi
LOCKLINT=${LOCKLINT:-"$SRC/tools/locklint/locklint"}
CF=${CF:-"$TESTDIR/usbprn-with-usba.cf"}
OUT=${OUT:-"$SRC/../../tmp/locklint-large-usb.out"}
DUMP_OPTIONS=
if [[ ${DUMPS:-0} == 1 ]]; then
	DUMP_OPTIONS='--dump-callgraph --dump-contexts
	    --dump-protection-states --dump-statistics --dump-types
	    --dump-policy-workload --times'
fi

SOURCES='
common/io/usb/usba/genconsole.c
common/io/usb/usba/hcdi.c
common/io/usb/usba/hubdi.c
common/io/usb/usba/parser.c
common/io/usb/usba/usba.c
common/io/usb/usba/usba10_calls.c
common/io/usb/usba/usba_bos.c
common/io/usb/usba/usba_devdb.c
common/io/usb/usba/usba_ugen.c
common/io/usb/usba/usbai.c
common/io/usb/usba/usbai_pipe_mgmt.c
common/io/usb/usba/usbai_register.c
common/io/usb/usba/usbai_req.c
common/io/usb/usba/usbai_util.c
common/io/usb/hubd/hubd.c
common/io/usb/hcd/openhci/ohci.c
common/io/usb/hcd/openhci/ohci_hub.c
common/io/usb/hcd/openhci/ohci_polled.c
common/io/usb/hcd/uhci/uhci.c
common/io/usb/hcd/uhci/uhcihub.c
common/io/usb/hcd/uhci/uhcipolled.c
common/io/usb/hcd/uhci/uhcitgt.c
common/io/usb/hcd/uhci/uhciutil.c
common/io/usb/hcd/ehci/ehci.c
common/io/usb/hcd/ehci/ehci_hub.c
common/io/usb/hcd/ehci/ehci_intr.c
common/io/usb/hcd/ehci/ehci_isoch.c
common/io/usb/hcd/ehci/ehci_isoch_util.c
common/io/usb/hcd/ehci/ehci_polled.c
common/io/usb/hcd/ehci/ehci_util.c
common/io/usb/hcd/ehci/ehci_xfer.c
common/io/usb/clients/printer/usbprn.c
'

if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "RunLargeUSB: locklint executable not found: $LOCKLINT"
	exit 1
fi
if [[ ! -r "$CF" ]]; then
	print -u2 "RunLargeUSB: command file not found: $CF"
	exit 1
fi

set --
for source in $SOURCES
do
	set -- "$@" "$SRC/uts/$source"
done

mkdir -p "${OUT%/*}" || exit 1
"$LOCKLINT" --compat=osll --check-locks $DUMP_OPTIONS --cf "$CF" \
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
    -I"$SRC/uts/intel" -nostdinc -I"$SRC/uts/common" \
    -mcmodel=kernel "$@" >"$OUT" 2>&1

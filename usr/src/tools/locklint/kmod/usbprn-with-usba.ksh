#!/bin/ksh
#
# Run native locklint over the large usbprn-with-USBA integration workload.
# This workload is intentionally separate from the focused tests run by
# "make test".  Set DUMPS=1 to include development dumps.  Every run reports
# locklint's total measured time.
# Source and include pathnames are passed relative to the repository root so
# diagnostics remain stable across workspaces.  SRC, LOCKLINT, CF, and OUT may
# override the defaults.
#

START_DIR=$(pwd) || exit 1
TESTDIR=$(cd "$(dirname "$0")" && pwd) || exit 1
if [[ -z "$SRC" ]]; then
	SRC=$(cd "$TESTDIR/../../.." && pwd) || exit 1
else
	SRC=$(cd "$SRC" && pwd) || exit 1
fi
LOCKLINT=${LOCKLINT:-"$SRC/tools/locklint/locklint"}
CF=${CF:-"$TESTDIR/usbprn-with-usba.cf"}
OUT=${OUT:-"$SRC/../../tmp/locklint-large-usb.out"}
case "$LOCKLINT" in
/*)
	;;
*)
	LOCKLINT="$START_DIR/$LOCKLINT"
	;;
esac
case "$CF" in
/*)
	;;
*)
	CF="$START_DIR/$CF"
	;;
esac
case "$OUT" in
/*)
	;;
*)
	OUT="$START_DIR/$OUT"
	;;
esac
REPO_ROOT=$(cd "$SRC/../.." && pwd) || exit 1
DUMP_OPTIONS=--times
if [[ ${DUMPS:-0} == 1 ]]; then
	DUMP_OPTIONS='--dump-callgraph --dump-contexts
	    --dump-protection-states --dump-statistics --dump-types --times'
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
	print -u2 "usbprn-with-usba: locklint executable not found: $LOCKLINT"
	exit 1
fi
if [[ ! -r "$CF" ]]; then
	print -u2 "usbprn-with-usba: command file not found: $CF"
	exit 1
fi

set --
for source in $SOURCES
do
	set -- "$@" "usr/src/uts/$source"
done

print "Running new locklint over usbprn+usba"

mkdir -p "${OUT%/*}" || exit 1
cd "$REPO_ROOT" || exit 1
"$LOCKLINT" --compat=osll --check-locks $DUMP_OPTIONS --cf "$CF" \
    -D__sun -m64 -Ui386 -U__i386 -O2 \
    -D_ASM_INLINES -std=gnu99 \
    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
    -Iusr/src/uts/intel -nostdinc -Iusr/src/uts/common \
     "$@" >"$OUT" 2>&1
status=$?

elapsed=$(awk '/^time total/ { print $3; exit }' "$OUT")
if (( status == 0 )); then
	result=PASS
else
	result=FAIL
fi
if [[ -n "$elapsed" ]]; then
	print "$result usbprn-with-usba: $elapsed seconds"
else
	print "$result usbprn-with-usba: timing unavailable"
fi
exit "$status"

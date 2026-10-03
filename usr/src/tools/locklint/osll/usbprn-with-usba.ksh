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
# Reproduce the 32-translation-unit usbprn-with-USBA analysis under Old
# Solaris Lock Lint.  Each run retains its compiler, session, analysis, and
# normalized output under tmp/osll/results and compares the result with the
# reviewed reference beside this script.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
SRC=${SRC:-"$REPO_ROOT/usr/src"}
RESULTS_ROOT="$REPO_ROOT/tmp/osll/results"
POLICY="$SCRIPT_DIR/usbprn-with-usba.wlcmd"
CORRECTIONS="$SCRIPT_DIR/usbprn-with-usba-corrections.wlcmd"
ADAPTER="$SCRIPT_DIR/usbprn-with-usba-adapter.c"
REFERENCE="$SCRIPT_DIR/usbprn-with-usba.ref"
NORMALIZER="$REPO_ROOT/usr/src/tools/locklint/tests/compare-lock-reports.py"

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"

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

MODEL_ROOTS='
hubd.c:hubd_info
hubd.c:hubd_open
hubd.c:hubd_close
hubd.c:hubd_ioctl
:usba_hubdi_attach
:usba_hubdi_detach
:usba_hubdi_power
hubdi.c:usba_hubdi_bus_ctl
hubdi.c:usba_hubdi_map_fault
hubdi.c:hubd_bus_config
hubdi.c:hubd_bus_unconfig
hubdi.c:hubd_busop_get_eventcookie
hubdi.c:hubd_busop_add_eventcall
hubdi.c:hubd_busop_remove_eventcall
hcdi.c:hcdi_soft_intr
ohci.c:ohci_info
ohci.c:ohci_attach
ohci.c:ohci_detach
ohci.c:ohci_open
ohci.c:ohci_close
ohci.c:ohci_ioctl
:ohci_hcdi_pm_support
ohci.c:ohci_xfer_timeout_handler
:ohci_handle_root_hub_status_change
uhci.c:uhci_info
uhci.c:uhci_attach
uhci.c:uhci_detach
uhci.c:uhci_reset
uhci.c:uhci_open
uhci.c:uhci_close
uhci.c:uhci_ioctl
uhci.c:uhci_intr
:uhci_cmd_timeout_hdlr
:uhci_handle_root_hub_status_change
ehci.c:ehci_info
ehci.c:ehci_attach
ehci.c:ehci_detach
ehci.c:ehci_reset
ehci.c:ehci_open
ehci.c:ehci_close
ehci.c:ehci_ioctl
:ehci_hcdi_pm_support
ehci_xfer.c:ehci_xfer_timeout_handler
ehci_hub.c:ehci_handle_root_hub_status_change
usbprn.c:usbprn_info
usbprn.c:usbprn_attach
usbprn.c:usbprn_detach
usbprn.c:usbprn_read
usbprn.c:usbprn_write
usbprn.c:usbprn_poll
'

run_quiet()
{
	"$@" >/dev/null
}

#
# Load one database while accepting the established status used when OSLL
# reports that a database is already represented in the combined context.
#
load_database()
{
	typeset database=$1
	typeset output
	typeset status

	output=$("$LOCK_LINT" load "$database" 2>&1)
	status=$?
	print "database=${database##*/} status=$status" >>"$OSLL_LOAD_OUT"
	if [[ -n "$output" ]]; then
		print -- "$output" >>"$OSLL_LOAD_OUT"
	fi
	case "$status" in
	0)
		return 0
		;;
	2)
		print -- "$output" | grep -q 'already loaded'
		return $?
		;;
	*)
		return "$status"
		;;
	esac
}

#
# Translate the retained historical command-file subset into the equivalent
# OSLL declarations.  Two stale declarations are omitted exactly as in the
# accepted characterization run.
#
apply_policy()
{
	typeset command_file
	typeset line

	for command_file in "$POLICY" "$CORRECTIONS"
	do
		while read line
		do
			set -- $line
			case "$1" in
			""|\#*)
				continue
				;;
			one)
				run_quiet "$LOCK_LINT" declare one "$2" ||
				    return $?
				;;
			root)
				if [[ "$2" == usb_parse_comp_ep_descr ]]; then
					continue
				fi
				run_quiet "$LOCK_LINT" declare root "$2" ||
				    return $?
				;;
			add)
				if [[ "$2" == hubd::h_cleanup_child ]]; then
					continue
				fi
				[[ "$3" == targets ]] || return 1
				run_quiet "$LOCK_LINT" declare "$2" targets "$4" ||
				    return $?
				;;
			*)
				print -u2 "unsupported historical command: $line"
				return 1
				;;
			esac
		done <"$command_file"
	done
}

#
# Execute inside the OSLL context created by "lock_lint start".  Raw findings
# and the function inventory remain available even when the reference differs.
#
run_session()
{
	typeset analyze_status
	typeset database
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	: >"$OSLL_LOAD_OUT" || exit 1
	for database in "$OSLL_DATABASE_DIR"/*.ll
	do
		load_database "$database" || exit $?
	done
	apply_policy || exit $?
	for root in $MODEL_ROOTS
	do
		run_quiet "$LOCK_LINT" declare root "$root" || exit $?
	done

	"$LOCK_LINT" analyze >"$OSLL_ANALYZE_RAW" 2>&1
	analyze_status=$?
	print -- "$analyze_status" >"$OSLL_ANALYZE_STATUS"
	case "$analyze_status" in
	0|5)
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac

	"$LOCK_LINT" funcs -o >"$OSLL_FUNCTIONS_RAW" 2>&1 || exit $?
	print ok >"$OSLL_SESSION_OK" || exit 1
	exit 0
}

if [[ "${OSLL_SESSION_MODE:-0}" == 1 ]]; then
	run_session
fi

#
# Generate source-compatible forms of three initialized tables that the
# historical frontend could not compile.  Blank replacement lines preserve
# all source locations used by the accepted reference.
#
generate_compatible_sources()
{
	typeset req_source="$SRC/uts/common/io/usb/usba/usbai_req.c"
	typeset ugen_source="$SRC/uts/common/io/usb/usba/usba_ugen.c"
	typeset util_source="$SRC/uts/common/io/usb/usba/usbai_util.c"

	if [[ "$(sed -n '4247p' "$ugen_source")" != \
	    '} ugen_cr2lcstat_table[] = {' ||
	    "$(sed -n '4269p' "$ugen_source")" != '};' ]]; then
		print -u2 "usba_ugen completion table source anchors changed"
		return 1
	fi
	awk '
	    NR == 4247 {
		    print "} ugen_cr2lcstat_table[21];"
		    skip = 1
		    next
	    }
	    skip && NR <= 4269 {
		    print ""
		    if (NR == 4269)
			    skip = 0
		    next
	    }
	    { print }
	' "$ugen_source" >"$OSLL_GENERATED_DIR/usba_ugen.c" || return 1

	if [[ "$(sed -n '388p' "$req_source")" != \
	    '} usb_invalid_flags_attrs[] = {' ||
	    "$(sed -n '405p' "$req_source")" != '};' ||
	    "$(sed -n '460p' "$req_source")" != '} rval2cr[] = {' ||
	    "$(sed -n '478p' "$req_source")" != '};' ]]; then
		print -u2 "usbai_req table source anchors changed"
		return 1
	fi
	awk '
	    NR == 388 {
		    print "} usb_invalid_flags_attrs[13];"
		    skip = 405
		    next
	    }
	    NR == 460 {
		    print "} rval2cr[17];"
		    skip = 478
		    next
	    }
	    skip != 0 && NR <= skip {
		    print ""
		    if (NR == skip)
			    skip = 0
		    next
	    }
	    { print }
	' "$req_source" >"$OSLL_GENERATED_DIR/usbai_req.c" || return 1

	if [[ "$(sed -n '1917p' "$util_source")" != \
	    '} usb_rval2errno_table[] = {' ||
	    "$(sed -n '1934p' "$util_source")" != '};' ]]; then
		print -u2 "usbai_util table source anchors changed"
		return 1
	fi
	awk '
	    NR == 1917 {
		    print "} usb_rval2errno_table[16];"
		    skip = 1934
		    next
	    }
	    skip != 0 && NR <= skip {
		    print ""
		    if (NR == skip)
			    skip = 0
		    next
	    }
	    { print }
	' "$util_source" >"$OSLL_GENERATED_DIR/usbai_util.c" || return 1
}

#
# Compile one source into an explicitly numbered database so basename
# collisions and database load order remain deterministic.
#
compile_source()
{
	typeset database=$1
	typeset prefix=$2
	typeset source=$3
	typeset produced="${source##*/}"
	produced="${produced%.c}.ll"

	set -- "$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common"
	if [[ -n "$prefix" ]]; then
		set -- "$@" -D_init=${prefix}_init -D_fini=${prefix}_fini \
		    -D_info=${prefix}_info
	fi
	"$@" "$source" >>"$OSLL_COMPILE_OUT" 2>&1 || return $?
	mv "$OSLL_BUILD_DIR/$produced" \
	    "$OSLL_DATABASE_DIR/$database" || return 1
}

for input in "$POLICY" "$CORRECTIONS" "$ADAPTER" "$REFERENCE" "$NORMALIZER"
do
	if [[ ! -r "$input" ]]; then
		print -u2 "required input not found: $input"
		exit 1
	fi
done
if [[ ! -x "$CC" ]]; then
	print -u2 "SunPro C compiler not found: $CC"
	exit 1
fi
if [[ ! -x "$LOCK_LINT" ]]; then
	print -u2 "OSLL command not found: $LOCK_LINT"
	exit 1
fi
if [[ ! -d "$SSBD_INCLUDE/sys" ]]; then
	print -u2 "OSLL annotation headers not found: $SSBD_INCLUDE"
	exit 1
fi

timestamp=$(date '+%Y%m%d-%H%M%S')
OSLL_RESULT_DIR="$RESULTS_ROOT/usbprn-with-usba-$timestamp-$$"
OSLL_BUILD_DIR="$OSLL_RESULT_DIR/build"
OSLL_DATABASE_DIR="$OSLL_RESULT_DIR/databases"
OSLL_GENERATED_DIR="$OSLL_RESULT_DIR/generated"
OSLL_CONTEXT_DIR="$OSLL_RESULT_DIR/context"
OSLL_COMPILE_OUT="$OSLL_RESULT_DIR/compile.out"
OSLL_LOAD_OUT="$OSLL_RESULT_DIR/load.out"
OSLL_ANALYZE_RAW="$OSLL_RESULT_DIR/analyze.raw"
OSLL_ANALYZE_STATUS="$OSLL_RESULT_DIR/analyze.status"
OSLL_FUNCTIONS_RAW="$OSLL_RESULT_DIR/functions.raw"
OSLL_SESSION_OUT="$OSLL_RESULT_DIR/session.out"
OSLL_SESSION_STATUS="$OSLL_RESULT_DIR/session.status"
OSLL_SESSION_OK="$OSLL_RESULT_DIR/session.ok"
OSLL_CURRENT_REFERENCE="$OSLL_RESULT_DIR/usbprn-with-usba.ref"
OSLL_REFERENCE_DIFF="$OSLL_RESULT_DIR/reference.diff"

mkdir -p "$OSLL_BUILD_DIR" "$OSLL_DATABASE_DIR" \
    "$OSLL_GENERATED_DIR" "$OSLL_CONTEXT_DIR" || exit 1
: >"$OSLL_COMPILE_OUT" || exit 1
generate_compatible_sources || exit 1

index=0
for relative in $SOURCES
do
	index=$((index + 1))
	source="$SRC/uts/$relative"
	case "$relative" in
	common/io/usb/usba/usba_ugen.c)
		source="$OSLL_GENERATED_DIR/usba_ugen.c"
		;;
	common/io/usb/usba/usbai_req.c)
		source="$OSLL_GENERATED_DIR/usbai_req.c"
		;;
	common/io/usb/usba/usbai_util.c)
		source="$OSLL_GENERATED_DIR/usbai_util.c"
		;;
	esac
	prefix=
	case "$relative" in
	common/io/usb/usba/usba.c)
		prefix=usba_module
		;;
	common/io/usb/hubd/hubd.c)
		prefix=hubd_module
		;;
	common/io/usb/hcd/openhci/ohci.c)
		prefix=ohci_module
		;;
	common/io/usb/hcd/uhci/uhci.c)
		prefix=uhci_module
		;;
	common/io/usb/hcd/ehci/ehci.c)
		prefix=ehci_module
		;;
	common/io/usb/clients/printer/usbprn.c)
		prefix=usbprn_module
		;;
	esac

	print "[$index/32] $relative"
	database=$(printf '%02d-%s.ll' "$index" \
	    "${relative##*/}" | sed 's/\.c\.ll$/.ll/')
	(
		cd "$OSLL_BUILD_DIR" || exit 1
		compile_source "$database" "$prefix" "$source"
	) || {
		status=$?
		print -u2 "OSLL compilation failed for $relative"
		print -u2 "see $OSLL_COMPILE_OUT"
		exit "$status"
	}
done

(
	cd "$OSLL_BUILD_DIR" || exit 1
	compile_source "33-adapter.ll" "" "$ADAPTER"
) || {
	status=$?
	print -u2 "OSLL compilation failed for $ADAPTER"
	print -u2 "see $OSLL_COMPILE_OUT"
	exit "$status"
}

OSLL_SESSION_MODE=1
TMPDIR="$OSLL_CONTEXT_DIR"
export SRC SUNPRO_BIN OSLL_SESSION_MODE OSLL_DATABASE_DIR OSLL_LOAD_OUT
export OSLL_ANALYZE_RAW OSLL_ANALYZE_STATUS OSLL_FUNCTIONS_RAW
export OSLL_SESSION_OK TMPDIR

print "Running Old Solaris Lock Lint"
"$LOCK_LINT" start "$SCRIPT" >"$OSLL_SESSION_OUT" 2>&1
session_status=$?
print -- "$session_status" >"$OSLL_SESSION_STATUS"
if (( session_status != 0 )) || [[ ! -f "$OSLL_SESSION_OK" ]]; then
	print -u2 "OSLL session failed with status $session_status"
	print -u2 "see $OSLL_SESSION_OUT"
	exit 1
fi

"$NORMALIZER" --from-osll="$OSLL_ANALYZE_RAW" \
    --osll-reference-output="$OSLL_CURRENT_REFERENCE" || {
	print -u2 "could not normalize OSLL findings"
	print -u2 "see $OSLL_ANALYZE_RAW"
	exit 1
}

if ! cmp -s "$REFERENCE" "$OSLL_CURRENT_REFERENCE"; then
	diff -u "$REFERENCE" "$OSLL_CURRENT_REFERENCE" \
	    >"$OSLL_REFERENCE_DIFF"
	print -u2 "OSLL findings differ from the reviewed reference"
	print -u2 "see $OSLL_REFERENCE_DIFF"
	print -u2 "results: $OSLL_RESULT_DIR"
	exit 1
fi

print "OSLL findings match the reviewed 348-record reference."
print "Results: $OSLL_RESULT_DIR"

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
# Compile temporary OSLL databases and capture protection and diagnostic
# evidence for the combined usbsacm and usbser modules under all four
# historical "one" declaration modes.  The runner writes reports into a
# prepared result bundle, removes its temporary products, and refuses to
# replace prior results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REPO_ROOT=$(CDPATH= cd "$SCRIPT_DIR/../../../../.." && pwd)
SRC="$REPO_ROOT/usr/src"
MODULE_DIR="$SRC/uts/intel/usbsacm"
OSLL_ONE_MODE=${OSLL_ONE_MODE:-both}

case "$OSLL_ONE_MODE" in
both|none|usbser|usbsacm)
	;;
*)
	print -u2 "OSLL_ONE_MODE must be both, none, usbser, or usbsacm"
	exit 1
	;;
esac

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbsacm module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"

OSLL_HISTORICAL_ROOTS='
:usbser_first_device
usbser.c:usbser_putchar
usbser.c:usbser_getchar
usbser.c:usbser_ischar
usbser.c:usbser_polledio_enter
usbser.c:usbser_polledio_exit
:usbser_soft_state_size
usbsacm.c:usbsacm_open
:usbser_close
:usbser_wput
:usbser_wsrv
:usbser_rsrv
usbser.c:usbser_tx_cb
usbser.c:usbser_rx_cb
usbser.c:usbser_status_cb
usbser.c:usbser_wq_thread
usbser.c:usbser_rq_thread
usbser.c:usbser_disconnect_cb
usbser.c:usbser_reconnect_cb
usbser.c:usbser_cpr_suspend
usbser.c:usbser_cpr_resume
usbsacm.c:usbsacm_bulkin_cb
usbsacm.c:usbsacm_bulkout_cb
usbsacm.c:usbsacm_intr_cb
usbsacm.c:usbsacm_intr_ex_cb
'

#
# Supply entry points reached by the historical pseudo-kernel rather than the
# command file, plus usbser_restart's timeout edge.
#
OSLL_MODEL_ROOTS='
:usbser_attach
:usbser_detach
:usbser_getinfo
:usbser_power
usbser.c:usbser_restart
usbsacm.c:usbsacm_attach
usbsacm.c:usbsacm_detach
usbsacm.c:usbsacm_getinfo
'

OSLL_USBSACM_FUNCTIONS='
usbsacm_module_init
usbsacm_module_fini
usbsacm_module_info
usbsacm_attach
usbsacm_detach
usbsacm_getinfo
usbsacm_open
usbsacm_ds_attach
usbsacm_ds_detach
usbsacm_ds_register_cb
usbsacm_ds_unregister_cb
usbsacm_ds_open_port
usbsacm_ds_close_port
usbsacm_ds_usb_power
usbsacm_ds_suspend
usbsacm_ds_resume
usbsacm_ds_disconnect
usbsacm_ds_reconnect
usbsacm_ds_set_port_params
usbsacm_ds_set_modem_ctl
usbsacm_ds_get_modem_ctl
usbsacm_ds_break_ctl
usbsacm_ds_tx
usbsacm_ds_rx
usbsacm_ds_stop
usbsacm_ds_start
usbsacm_ds_fifo_flush
usbsacm_ds_fifo_drain
usbsacm_fifo_flush_locked
usbsacm_get_bulk_pipe_number
usbsacm_init_ports_status
usbsacm_init_alloc_ports
usbsacm_free_ports
usbsacm_get_descriptors
usbsacm_cleanup
usbsacm_restore_device_state
usbsacm_restore_port_state
usbsacm_open_port_pipes
usbsacm_close_port_pipes
usbsacm_close_pipes
usbsacm_disconnect_pipes
usbsacm_reconnect_pipes
usbsacm_bulkin_cb
usbsacm_bulkout_cb
usbsacm_rx_start
usbsacm_tx_start
usbsacm_send_data
usbsacm_wait_tx_drain
usbsacm_req_write
usbsacm_set_line_coding
usbsacm_mctl2reg
usbsacm_reg2mctl
usbsacm_put_tail
usbsacm_put_head
usbsacm_create_pm_components
usbsacm_destroy_pm_components
usbsacm_pm_set_busy
usbsacm_pm_set_idle
usbsacm_pwrlvl0
usbsacm_pwrlvl1
usbsacm_pwrlvl2
usbsacm_pwrlvl3
usbsacm_pipe_start_polling
usbsacm_intr_cb
usbsacm_intr_ex_cb
usbsacm_parse_intr_data
'

OSLL_DS_TARGETS='
ds_attach usbsacm.c:usbsacm_ds_attach
ds_detach usbsacm.c:usbsacm_ds_detach
ds_register_cb usbsacm.c:usbsacm_ds_register_cb
ds_unregister_cb usbsacm.c:usbsacm_ds_unregister_cb
ds_open_port usbsacm.c:usbsacm_ds_open_port
ds_close_port usbsacm.c:usbsacm_ds_close_port
ds_usb_power usbsacm.c:usbsacm_ds_usb_power
ds_suspend usbsacm.c:usbsacm_ds_suspend
ds_resume usbsacm.c:usbsacm_ds_resume
ds_disconnect usbsacm.c:usbsacm_ds_disconnect
ds_reconnect usbsacm.c:usbsacm_ds_reconnect
ds_set_port_params usbsacm.c:usbsacm_ds_set_port_params
ds_set_modem_ctl usbsacm.c:usbsacm_ds_set_modem_ctl
ds_get_modem_ctl usbsacm.c:usbsacm_ds_get_modem_ctl
ds_break_ctl usbsacm.c:usbsacm_ds_break_ctl
ds_tx usbsacm.c:usbsacm_ds_tx
ds_rx usbsacm.c:usbsacm_ds_rx
ds_stop usbsacm.c:usbsacm_ds_stop
ds_start usbsacm.c:usbsacm_ds_start
ds_fifo_flush usbsacm.c:usbsacm_ds_fifo_flush
ds_fifo_drain usbsacm.c:usbsacm_ds_fifo_drain
'

OSLL_RSEQ_TARGETS='
usbser.c:usbser_free_soft_state
usbser.c:usbser_init_soft_state
usbser.c:usbser_fini_soft_state
usbser.c:usbser_attach_dev
usbser.c:usbser_detach_dev
usbser.c:usbser_attach_ports
usbser.c:usbser_detach_ports
usbser.c:usbser_create_taskq
usbser.c:usbser_destroy_taskq
'

OSLL_EXPECTED_DUPLICATES='function ystm.h:sum_overflows_u16 already loaded;
skipping this definition [systm.h,332]
function ystm.h:sum_overflows_hrtime already loaded;
skipping this definition [systm.h,338]
function ystm.h:sum_overflows_off already loaded;
skipping this definition [systm.h,345]'

run_quiet()
{
	"$@" >/dev/null
}

run_load()
{
	file=$1
	name=$2
	allow_duplicates=$3

	load_output=$("$LOCK_LINT" load "$file" 2>&1)
	status=$?
	print -r -- "$load_output" >"$DIAGNOSTICS/load-$name.out"
	print -- "$status" >"$DIAGNOSTICS/load-$name.status"
	case "$status" in
	0)
		return 0
		;;
	2)
		if [[ "$allow_duplicates" == yes &&
		    "$load_output" == "$OSLL_EXPECTED_DUPLICATES" ]]; then
			return 0
		fi
		return "$status"
		;;
	*)
		return "$status"
		;;
	esac
}

verify_source_annotations()
{
	"$LOCK_LINT" vars -a locklint_usbsacm_annotation_probe::value \
	    >"$DIAGNOSTICS/annotation.out" 2>&1
	status=$?
	print -- "$status" >"$DIAGNOSTICS/annotation.status"
	if (( status != 0 )) ||
	    ! grep -q \
	    '^locklint_usbsacm_annotation_probe::value[[:space:]].*assert=locklint_usbsacm_annotation_probe::lock$' \
	    "$DIAGNOSTICS/annotation.out"
	then
		return 1
	fi

	"$LOCK_LINT" vars -a usbsacm_state::acm_dev_state \
	    >"$DIAGNOSTICS/source-annotation.out" 2>&1
	status=$?
	print -- "$status" >"$DIAGNOSTICS/source-annotation.status"
	if (( status != 0 )) ||
	    ! grep -q \
	    '^usbsacm_state::acm_dev_state[[:space:]].*assert=usbsacm_state::acm_mutex$' \
	    "$DIAGNOSTICS/source-annotation.out"
	then
		return 1
	fi
}

verify_module_coverage()
{
	typeset function
	typeset missing=
	typeset count

	for function in $OSLL_USBSACM_FUNCTIONS
	do
		if ! grep -q \
		    "^[^[:space:]]*:${function}[[:space:]].*\\[usbsacm.c,[0-9][0-9]*\\]" \
		    "$DIAGNOSTICS/functions.out"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL usbsacm function inventory omitted:$missing"
		return 1
	fi

	if grep -q \
	    '\[usbser\(_rseq\)\{0,1\}.c,[0-9][0-9]*\].*=unanalyzed\|\[usbsacm.c,[0-9][0-9]*\].*=unanalyzed' \
	    "$DIAGNOSTICS/functions.out"
	then
		print -u2 "OSLL left combined module functions unanalyzed"
		return 1
	fi

	count=$(grep -Ec \
	    '\[(usbser(_rseq)?|usbsacm)\.c,[0-9]+\]' \
	    "$DIAGNOSTICS/functions.out")
	if (( count != 156 )); then
		print -u2 "OSLL combined inventory has $count functions, expected 156"
		return 1
	fi
}

write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbser_state::/, "", member)
		sub(/^usbser_port::/, "", member)
		sub(/^usbsacm_state::/, "", member)
		sub(/^usbsacm_port::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[(usbser|usbsacm)\.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$DIAGNOSTICS/analyze.out"
}

declare_indirect_targets()
{
	typeset member
	typeset target

	set -- $OSLL_DS_TARGETS
	while (( $# != 0 ))
	do
		member=$1
		target=$2
		shift 2
		run_quiet "$LOCK_LINT" declare "ds_ops::$member" targets \
		    "$target" || return $?
	done

	for member in ds_in_pipe ds_out_pipe
	do
		run_quiet "$LOCK_LINT" declare "ds_ops::$member" targets \
		    :locklint_usbsacm_warlock_dummy || return $?
	done

	run_quiet "$LOCK_LINT" declare rseq_step::s_func targets \
	    $OSLL_RSEQ_TARGETS || return $?
	run_quiet "$LOCK_LINT" declare usbsacm_port::acm_cb.cb_tx targets \
	    usbser.c:usbser_tx_cb || return $?
	run_quiet "$LOCK_LINT" declare usbsacm_port::acm_cb.cb_rx targets \
	    usbser.c:usbser_rx_cb
}

run_session()
{
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		exit 1
	fi
	cd "$REPO_ROOT" || exit 1

	run_load "$OSLL_USBSER_LL" usbser no || exit $?
	run_load "$OSLL_RSEQ_LL" rseq no || exit $?
	run_load "$OSLL_USBSACM_LL" usbsacm yes || exit $?
	run_load "$OSLL_ADAPTER_LL" adapter no || exit $?
	verify_source_annotations || exit $?

	{
		declare_indirect_targets || exit $?
		case "$OSLL_ONE_MODE" in
		both)
			"$LOCK_LINT" declare one usbser_state || exit $?
			"$LOCK_LINT" declare one usbsacm_state || exit $?
			;;
		usbser)
			"$LOCK_LINT" declare one usbser_state || exit $?
			;;
		usbsacm)
			"$LOCK_LINT" declare one usbsacm_state || exit $?
			;;
		esac

		for root in $OSLL_HISTORICAL_ROOTS $OSLL_MODEL_ROOTS
		do
			"$LOCK_LINT" declare root "$root" || exit $?
		done
	} >"$DIAGNOSTICS/configure.out" 2>&1
	status=$?
	print -- "$status" >"$DIAGNOSTICS/configure.status"
	(( status == 0 )) || exit "$status"

	"$LOCK_LINT" analyze >"$DIAGNOSTICS/analyze.out" 2>&1
	status=$?
	print -- "$status" >"$DIAGNOSTICS/analyze.status"
	(( status == 0 || status == 5 )) || exit "$status"

	"$LOCK_LINT" funcs -o >"$DIAGNOSTICS/functions.out" 2>&1
	status=$?
	print -- "$status" >"$DIAGNOSTICS/functions.status"
	(( status == 0 )) || exit "$status"
	verify_module_coverage || exit 1

	"$LOCK_LINT" vars -a >"$PROTECTION/vars-a.out" 2>&1
	status=$?
	print -- "$status" >"$PROTECTION/vars-a.status"
	(( status == 0 || status == 5 )) || exit "$status"

	"$LOCK_LINT" vars -h >"$PROTECTION/vars-h.out" 2>&1
	status=$?
	print -- "$status" >"$PROTECTION/vars-h.status"
	(( status == 0 || status == 5 )) || exit "$status"

	write_summary >"$DIAGNOSTICS/report.out"
	print -- "$?" >"$DIAGNOSTICS/report.status"
	print ok >"$PROTECTION/session.ok"
}

if [[ "${OSLL_AUDIT_SESSION:-0}" == 1 ]]; then
	run_session
	exit $?
fi

if (( $# != 1 )); then
	print -u2 "usage: $0 repo-relative-result-directory"
	exit 2
fi
RESULT=$1
case "$RESULT" in
/*)
	print -u2 "result directory must be repository-relative"
	exit 2
	;;
esac

TMPDIR="$REPO_ROOT/$RESULT/osll-protection/tmp"
WORK_DIR="$TMPDIR"
GENERATED_SOURCE_DIR="$TMPDIR/source"
OSLL_USBSER_LL="$TMPDIR/usbser.ll"
OSLL_RSEQ_LL="$TMPDIR/usbser_rseq.ll"
OSLL_USBSACM_LL="$TMPDIR/usbsacm.ll"
OSLL_ADAPTER_LL="$TMPDIR/check-kmod-usbsacm-adapter.ll"

if [[ ! -x "$CC" || ! -x "$LOCK_LINT" ||
    ! -d "$SSBD_INCLUDE/sys" || ! -d "$MODULE_DIR" ]]; then
	print -u2 "required OSLL tool or combined module input is unavailable"
	exit 1
fi

mkdir -p "$TMPDIR" || exit 1
cd "$MODULE_DIR" || exit 1
for database in usbser.ll usbser_rseq.ll usbsacm.ll \
    check-kmod-usbsacm-adapter.ll
do
	if [[ -e "$database" ]]; then
		print -u2 "usbsacm module directory already contains $database"
		exit 1
	fi
done

cleanup_products()
{
	rm -f "$OSLL_USBSER_LL" "$OSLL_RSEQ_LL" "$OSLL_USBSACM_LL" \
	    "$OSLL_ADAPTER_LL" "$GENERATED_SOURCE_DIR/usbsacm.c"
	rmdir "$GENERATED_SOURCE_DIR" "$TMPDIR" 2>/dev/null
}

trap cleanup_products EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

compile_source()
{
	source=$1
	database=$2
	shift 2

	"$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -D__lock_lint=1 "$@" \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$WORK_DIR/$database" || return 1
	fi
	return "$status"
}

prepare_usbsacm_source()
{
	source=../../common/io/usb/clients/usbser/usbsacm/usbsacm.c
	generated="$GENERATED_SOURCE_DIR/usbsacm.c"

	if [[ $(sed -n '450p' "$source") != \
	    'static int usbsacm_speedtab[] = {' ]] ||
	    [[ $(sed -n '483p' "$source") != '};' ]]; then
		print -u2 "usbsacm speed-table source anchors changed"
		return 1
	fi

	mkdir -p "$GENERATED_SOURCE_DIR" || return 1
	awk '
	    NR == 450 {
		print "static int usbsacm_speedtab[32];"
		next
	    }
	    NR > 450 && NR <= 483 {
		print ""
		next
	    }
	    {
		print
	    }
	' "$source" >"$generated"
}

print "Compiling usbsacm and usbser sources for OSLL"
prepare_usbsacm_source &&
    compile_source ../../common/io/usb/clients/usbser/usbser.c usbser.ll \
    -D_init=usbser_module_init -D_fini=usbser_module_fini \
    -D_info=usbser_module_info &&
    compile_source ../../common/io/usb/clients/usbser/usbser_rseq.c \
    usbser_rseq.ll &&
    compile_source "$GENERATED_SOURCE_DIR/usbsacm.c" usbsacm.ll \
    -D_init=usbsacm_module_init -D_fini=usbsacm_module_fini \
    -D_info=usbsacm_module_info &&
    compile_source "$SCRIPT_DIR/check-kmod-usbsacm-adapter.c" \
    check-kmod-usbsacm-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

if [[ ! -f "$OSLL_USBSER_LL" || ! -f "$OSLL_RSEQ_LL" ||
    ! -f "$OSLL_USBSACM_LL" || ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

for OSLL_ONE_MODE in both none usbser usbsacm
do
	PROTECTION="$REPO_ROOT/$RESULT/osll-protection/$OSLL_ONE_MODE"
	DIAGNOSTICS="$REPO_ROOT/$RESULT/osll-diagnostics/$OSLL_ONE_MODE"
	REFERENCE="$DIAGNOSTICS/accepted-reference.out"
	if [[ ! -f "$REFERENCE" ]]; then
		print -u2 "required input not found: $REFERENCE"
		exit 1
	fi
	for file in "$PROTECTION/session.out" "$PROTECTION/session.status" \
	    "$PROTECTION/session.ok" "$PROTECTION/vars-a.out" \
	    "$PROTECTION/vars-h.out" "$DIAGNOSTICS/analyze.out" \
	    "$DIAGNOSTICS/functions.out" "$DIAGNOSTICS/report.out"
	do
		if [[ -e "$file" ]]; then
			print -u2 "refusing to overwrite result: $file"
			exit 1
		fi
	done

	OSLL_AUDIT_SESSION=1
	export REPO_ROOT SRC OSLL_ONE_MODE OSLL_AUDIT_SESSION
	export OSLL_USBSER_LL OSLL_RSEQ_LL OSLL_USBSACM_LL OSLL_ADAPTER_LL
	export PROTECTION DIAGNOSTICS TMPDIR

	print "Running OSLL over usbsacm and usbser ($OSLL_ONE_MODE one mode)"
	"$LOCK_LINT" start "$SCRIPT" >"$PROTECTION/session.out" 2>&1
	status=$?
	print -- "$status" >"$PROTECTION/session.status"
	if (( status != 0 )) || [[ ! -f "$PROTECTION/session.ok" ]]; then
		exit 1
	fi

	awk '$0 !~ /^#/' "$REFERENCE" \
	    >"$DIAGNOSTICS/accepted-reference.filtered.out" || exit 1
	if cmp -s "$DIAGNOSTICS/accepted-reference.filtered.out" \
	    "$DIAGNOSTICS/report.out"; then
		print 0 >"$DIAGNOSTICS/reference.status"
	else
		diff -u "$DIAGNOSTICS/accepted-reference.filtered.out" \
		    "$DIAGNOSTICS/report.out" \
		    >"$DIAGNOSTICS/reference.diff"
		print 1 >"$DIAGNOSTICS/reference.status"
		exit 1
	fi
done
print "OSLL usbsacm audit results: $RESULT"

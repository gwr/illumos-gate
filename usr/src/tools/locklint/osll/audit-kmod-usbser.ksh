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
# evidence for the complete usbser module.  The runner writes reports into a
# prepared result bundle, removes the databases, and refuses to replace prior
# results.
#

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
script="$script_dir/$(basename "$0")"
repo_root=$(CDPATH= cd "$script_dir/../../../../.." && pwd)
src="$repo_root/usr/src"
module_dir="$src/uts/intel/usbser"
sunpro_bin=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
cc="$sunpro_bin/cc"
lock_lint="$sunpro_bin/lock_lint"
ssbd_include="$sunpro_bin/../prod/include/cc/ssbd"
roots='
:usbser_first_device
usbser.c:usbser_putchar
usbser.c:usbser_getchar
usbser.c:usbser_ischar
usbser.c:usbser_polledio_enter
usbser.c:usbser_polledio_exit
:usbser_soft_state_size
:usbser_open
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
:usbser_attach
:usbser_detach
:usbser_getinfo
:usbser_power
usbser.c:usbser_restart
'
functions='
_init
_fini
_info
usbser_soft_state_size
usbser_getinfo
usbser_insert
usbser_remove
usbser_first_device
usbser_attach
usbser_detach
usbser_open
usbser_close
usbser_rsrv
usbser_wput
usbser_wsrv
usbser_power
usbser_rseq_do_cb
usbser_free_soft_state
usbser_init_soft_state
usbser_fini_soft_state
usbser_attach_dev
usbser_detach_dev
usbser_attach_ports
usbser_create_port_minor_nodes
usbser_detach_ports
usbser_create_taskq
usbser_destroy_taskq
usbser_set_dev_state_init
usbser_disconnect_cb
usbser_reconnect_cb
usbser_disconnect_ports
usbser_cpr_suspend
usbser_suspend_ports
usbser_cpr_resume
usbser_restore_device_state
usbser_restore_ports_state
usbser_open_setup
usbser_open_init
usbser_check_port_props
usbser_open_fini
usbser_open_line_setup
usbser_open_carrier_check
usbser_open_queues_init
usbser_open_queues_fini
usbser_close_drain
usbser_close_cancel_break
usbser_close_hangup
usbser_close_cleanup
usbser_thr_dispatch
usbser_thr_cancel
usbser_thr_wake
usbser_wq_thread
usbser_rq_thread
usbser_tx_cb
usbser_rx_cb
usbser_rx_massage_data
usbser_rx_massage_mbreak
usbser_rx_cb_put
usbser_status_cb
usbser_status_proc_cb
usbser_wmsg
usbser_data
usbser_ioctl
usbser_iocdata
usbser_stop
usbser_start
usbser_stopi
usbser_starti
usbser_flush
usbser_break
usbser_delay
usbser_restart
usbser_port_program
usbser_inbound_flow_ctl
usbser_dev_is_online
usbser_serialize_port_act
usbser_release_port_act
usbser_msgtype2str
usbser_ioctl2str
usbser_polledio_init
usbser_polledio_fini
usbser_putchar
usbser_getchar
usbser_ischar
usbser_polledio_enter
usbser_polledio_exit
rseq_do_common
rseq_undo_common
rseq_do
rseq_undo
'
ds_members='
ds_attach
ds_detach
ds_register_cb
ds_unregister_cb
ds_open_port
ds_close_port
ds_usb_power
ds_suspend
ds_resume
ds_disconnect
ds_reconnect
ds_set_port_params
ds_set_modem_ctl
ds_get_modem_ctl
ds_break_ctl
ds_loopback
ds_tx
ds_rx
ds_stop
ds_start
ds_fifo_flush
ds_fifo_drain
ds_out_pipe
ds_in_pipe
'
rseq_targets='
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

#
# Compile one source file and move its temporary database out of the source
# directory.
#
compile_source()
{
	source=$1
	database=$2

	"$cc" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -D__lock_lint=1 \
	    -I"$ssbd_include" -I"$src/uts/intel" -I"$src/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$TMPDIR/$database" || return 1
	fi
	return "$status"
}

#
# Remove database build products without disturbing captured reports.
#
cleanup_databases()
{
	rm -f "$usbser_ll" "$rseq_ll" "$adapter_ll"
}

#
# Load one database while preserving its output and exact status.
#
run_load()
{
	file=$1
	name=$2

	"$lock_lint" load "$file" >"$diagnostics/$name.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/$name.status"
	return "$status"
}

#
# Require every accepted module function to be present and analyzed.
#
verify_coverage()
{
	for function in $functions
	do
		case "$function" in
		_init|_fini|_info|rseq_do|rseq_undo|usbser_attach|\
		usbser_close|usbser_detach|usbser_first_device|usbser_getinfo|\
		usbser_open|usbser_power|usbser_rsrv|usbser_soft_state_size|\
		usbser_wput|usbser_wsrv)
			symbol=":$function"
			;;
		rseq_do_common|rseq_undo_common)
			symbol="usbser_rseq.c:$function"
			;;
		*)
			symbol="usbser.c:$function"
			;;
		esac
		grep -q \
		    "^${symbol}[[:space:]].*\\[usbser\\(_rseq\\)\\{0,1\\}.c,[0-9][0-9]*\\]" \
		    "$diagnostics/functions.out" || return 1
	done
	! grep -q \
	    '\[usbser\(_rseq\)\{0,1\}.c,[0-9][0-9]*\].*=unanalyzed' \
	    "$diagnostics/functions.out"
}

#
# Reduce OSLL diagnostics to the accepted source-location/member report.
#
write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbser_state::/, "", member)
		sub(/^usbser_port::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbser.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$diagnostics/analyze.out"
}

#
# Supply the historical generic driver targets and exact recovery targets.
#
declare_indirect_targets()
{
	for member in $ds_members
	do
		"$lock_lint" declare "ds_ops::$member" targets \
		    :locklint_usbser_warlock_dummy || return $?
	done

	"$lock_lint" declare rseq_step::s_func targets $rseq_targets
}

#
# Run all declarations, analysis, and reports in one OSLL session.
#
run_session()
{
	[[ -n "$LL_CONTEXT" ]] || exit 1
	cd "$repo_root" || exit 1

	run_load "$usbser_ll" load-usbser || exit $?
	run_load "$rseq_ll" load-rseq || exit $?
	run_load "$adapter_ll" load-adapter || exit $?
	"$lock_lint" vars -a locklint_usbser_annotation_probe::value \
	    >"$diagnostics/annotation.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/annotation.status"
	(( status == 0 )) || exit "$status"
	grep -q \
	    '^locklint_usbser_annotation_probe::value[[:space:]].*assert=locklint_usbser_annotation_probe::lock$' \
	    "$diagnostics/annotation.out" || exit 1

	{
		declare_indirect_targets || exit $?
		"$lock_lint" declare one usbser_state || exit $?
		for root in $roots
		do
			"$lock_lint" declare root "$root" || exit $?
		done
	} >"$diagnostics/configure.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/configure.status"
	(( status == 0 )) || exit "$status"

	"$lock_lint" analyze >"$diagnostics/analyze.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/analyze.status"
	(( status == 0 || status == 5 )) || exit "$status"

	"$lock_lint" funcs -o >"$diagnostics/functions.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/functions.status"
	(( status == 0 )) || exit "$status"
	verify_coverage || exit 1

	"$lock_lint" vars -a >"$protection/vars-a.out" 2>&1
	status=$?
	print -- "$status" >"$protection/vars-a.status"
	(( status == 0 || status == 5 )) || exit "$status"

	"$lock_lint" vars -h >"$protection/vars-h.out" 2>&1
	status=$?
	print -- "$status" >"$protection/vars-h.status"
	(( status == 0 || status == 5 )) || exit "$status"

	write_summary >"$diagnostics/report.out"
	print -- "$?" >"$diagnostics/report.status"
	print ok >"$protection/session.ok"
}

if [[ "${OSLL_AUDIT_SESSION:-0}" == 1 ]]; then
	run_session
	exit $?
fi

if (( $# != 1 )); then
	print -u2 "usage: $0 repo-relative-result-directory"
	exit 2
fi
result=$1
case "$result" in
/*)
	print -u2 "result directory must be repository-relative"
	exit 2
	;;
esac

cd "$repo_root" || exit 1
protection="$repo_root/$result/osll-protection"
diagnostics="$repo_root/$result/osll-diagnostics"
TMPDIR="$protection/tmp"
usbser_ll="$TMPDIR/usbser.ll"
rseq_ll="$TMPDIR/usbser_rseq.ll"
adapter_ll="$TMPDIR/check-kmod-usbser-adapter.ll"
reference="$diagnostics/accepted-reference.out"
if [[ ! -f "$reference" ]]; then
	print -u2 "required input not found: $reference"
	exit 1
fi
for file in "$usbser_ll" "$rseq_ll" "$adapter_ll" \
    "$protection/session.out" "$protection/session.status" \
    "$protection/session.ok" "$protection/vars-a.out" \
    "$protection/vars-h.out" "$diagnostics/analyze.out" \
    "$diagnostics/functions.out" "$diagnostics/report.out"
do
	if [[ -e "$file" ]]; then
		print -u2 "refusing to overwrite result: $file"
		exit 1
	fi
done
if [[ ! -x "$cc" ]]; then
	print -u2 "SunPro C compiler not found: $cc"
	exit 1
fi
if [[ ! -x "$lock_lint" ]]; then
	print -u2 "OSLL command not found: $lock_lint"
	exit 1
fi
if [[ ! -d "$ssbd_include/sys" ]]; then
	print -u2 "OSLL annotation headers not found: $ssbd_include"
	exit 1
fi
if [[ ! -d "$module_dir" ]]; then
	print -u2 "usbser module directory not found: $module_dir"
	exit 1
fi

mkdir -p "$TMPDIR" || exit 1
cd "$module_dir" || exit 1
for database in usbser.ll usbser_rseq.ll check-kmod-usbser-adapter.ll
do
	if [[ -e "$database" ]]; then
		print -u2 "usbser module directory already contains $database"
		exit 1
	fi
done

trap cleanup_databases EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

print "Compiling usbser sources for OSLL"
compile_source ../../common/io/usb/clients/usbser/usbser.c usbser.ll &&
    compile_source ../../common/io/usb/clients/usbser/usbser_rseq.c \
    usbser_rseq.ll &&
    compile_source "$script_dir/check-kmod-usbser-adapter.c" \
    check-kmod-usbser-adapter.ll
status=$?
if (( status != 0 )); then
	print -u2 "OSLL compilation failed with status $status"
	exit "$status"
fi
if [[ ! -f "$usbser_ll" || ! -f "$rseq_ll" ||
    ! -f "$adapter_ll" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_AUDIT_SESSION=1
export OSLL_AUDIT_SESSION protection diagnostics usbser_ll rseq_ll adapter_ll
export TMPDIR
"$lock_lint" start "$script" >"$protection/session.out" 2>&1
status=$?
print -- "$status" >"$protection/session.status"
if (( status != 0 )) || [[ ! -f "$protection/session.ok" ]]; then
	exit 1
fi

awk '$0 !~ /^#/' "$reference" \
    >"$diagnostics/accepted-reference.filtered.out" || exit 1
if cmp -s "$diagnostics/accepted-reference.filtered.out" \
    "$diagnostics/report.out"; then
	print 0 >"$diagnostics/reference.status"
else
	diff -u "$diagnostics/accepted-reference.filtered.out" \
	    "$diagnostics/report.out" \
	    >"$diagnostics/reference.diff"
	print 1 >"$diagnostics/reference.status"
	exit 1
fi
print "OSLL usbser audit results: $result"

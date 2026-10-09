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
# evidence for the complete usbecm module.  The runner writes reports into a
# prepared result bundle, removes the databases, and refuses to replace prior
# results.
#

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
script="$script_dir/$(basename "$0")"
repo_root=$(CDPATH= cd "$script_dir/../../../../.." && pwd)
src="$repo_root/usr/src"
module_dir="$src/uts/intel/usbecm"
sunpro_bin=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
cc="$sunpro_bin/cc"
lock_lint="$sunpro_bin/lock_lint"
ssbd_include="$sunpro_bin/../prod/include/cc/ssbd"
roots='
usbecm.c:usbecm_bulkin_cb
usbecm.c:usbecm_bulkout_cb
usbecm.c:usbecm_intr_cb
usbecm.c:usbecm_intr_ex_cb
usbecm.c:usbecm_pm_set_busy
usbecm.c:usbecm_pm_set_idle
usbecm.c:usbecm_power
usbecm.c:usbecm_m_stop
usbecm.c:usbecm_m_start
usbecm.c:usbecm_m_unicst
usbecm.c:usbecm_m_multicst
usbecm.c:usbecm_m_promisc
usbecm.c:usbecm_m_ioctl
usbecm.c:usbecm_m_tx
usbecm.c:usbecm_m_getprop
usbecm.c:usbecm_m_setprop
usbecm.c:usbecm_m_stat
usbecm.c:usbecm_reconnect_event_cb
usbecm.c:usbecm_disconnect_event_cb
usbecm.c:usbecm_attach
usbecm.c:usbecm_detach
'
functions='
_init
_fini
_info
generate_ether_addr
label_to_mac
usbecm_attach
usbecm_bulkin_cb
usbecm_bulkout_cb
usbecm_cleanup
usbecm_close_pipes
usbecm_create_pm_components
usbecm_ctrl_read
usbecm_ctrl_write
usbecm_destroy_pm_components
usbecm_detach
usbecm_disconnect_event_cb
usbecm_find_bulk_in_out_eps
usbecm_get_descriptors
usbecm_get_statistics
usbecm_init_non_compatible_device
usbecm_intr_cb
usbecm_intr_ex_cb
usbecm_is_compatible
usbecm_m_getprop
usbecm_m_ioctl
usbecm_m_multicst
usbecm_m_promisc
usbecm_m_setprop
usbecm_m_start
usbecm_m_stat
usbecm_m_stop
usbecm_m_tx
usbecm_m_unicst
usbecm_mac_fini
usbecm_mac_init
usbecm_open_pipes
usbecm_parse_intr_data
usbecm_pipe_start_polling
usbecm_pm_set_busy
usbecm_pm_set_idle
usbecm_power
usbecm_pwrlvl0
usbecm_pwrlvl1
usbecm_pwrlvl2
usbecm_pwrlvl3
usbecm_reconnect_event_cb
usbecm_restore_device_state
usbecm_resume
usbecm_rx_start
usbecm_send_data
usbecm_send_zero_data
usbecm_suspend
usbecm_usb_init
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
# Remove the database build products without disturbing captured reports.
#
cleanup_databases()
{
	rm -f "$usbecm_ll" "$adapter_ll"
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
		_init|_fini|_info|label_to_mac|usbecm_find_bulk_in_out_eps)
			symbol=":$function"
			;;
		*)
			symbol="usbecm.c:$function"
			;;
		esac
		grep -q \
		    "^${symbol}[[:space:]].*\\[usbecm.c,[0-9][0-9]*\\]" \
		    "$diagnostics/functions.out" || return 1
	done
	! grep -q '\[usbecm.c,[0-9][0-9]*\].*=unanalyzed' \
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
		sub(/^usbecm_state::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbecm.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$diagnostics/analyze.out"
}

#
# Supply typed no-effect targets for the otherwise unassigned driver hooks.
#
declare_ds_targets()
{
	"$lock_lint" declare usbecm_ds_ops::ecm_ds_init targets \
	    :locklint_usbecm_ds_init || return $?
	"$lock_lint" declare usbecm_ds_ops::ecm_ds_fini targets \
	    :locklint_usbecm_ds_fini || return $?
	"$lock_lint" declare usbecm_ds_ops::ecm_ds_start targets \
	    :locklint_usbecm_ds_start || return $?
	"$lock_lint" declare usbecm_ds_ops::ecm_ds_stop targets \
	    :locklint_usbecm_ds_stop || return $?
	"$lock_lint" declare usbecm_ds_ops::ecm_ds_intr_cb targets \
	    :locklint_usbecm_ds_intr_cb
}

#
# Run all declarations, analysis, and reports in one OSLL session.
#
run_session()
{
	[[ -n "$LL_CONTEXT" ]] || exit 1
	cd "$repo_root" || exit 1

	run_load "$usbecm_ll" load-usbecm || exit $?
	run_load "$adapter_ll" load-adapter || exit $?
	"$lock_lint" vars -a locklint_usbecm_annotation_probe::value \
	    >"$diagnostics/annotation.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/annotation.status"
	(( status == 0 )) || exit "$status"
	grep -q \
	    '^locklint_usbecm_annotation_probe::value[[:space:]].*assert=locklint_usbecm_annotation_probe::lock$' \
	    "$diagnostics/annotation.out" || exit 1

	{
		declare_ds_targets || exit $?
		"$lock_lint" declare one usbecm_state || exit $?
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
usbecm_ll="$TMPDIR/usbecm.ll"
adapter_ll="$TMPDIR/check-kmod-usbecm-adapter.ll"
reference="$diagnostics/accepted-reference.out"
if [[ ! -f "$reference" ]]; then
	print -u2 "required input not found: $reference"
	exit 1
fi
for file in "$usbecm_ll" "$adapter_ll" \
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
	print -u2 "usbecm module directory not found: $module_dir"
	exit 1
fi

mkdir -p "$TMPDIR" || exit 1
cd "$module_dir" || exit 1
if [[ -e usbecm.ll || -e check-kmod-usbecm-adapter.ll ]]; then
	print -u2 "usbecm module directory already contains an OSLL database"
	exit 1
fi

trap cleanup_databases EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

print "Compiling usbecm sources for OSLL"
compile_source ../../common/io/usb/clients/usbecm/usbecm.c usbecm.ll &&
    compile_source "$script_dir/check-kmod-usbecm-adapter.c" \
    check-kmod-usbecm-adapter.ll
status=$?
if (( status != 0 )); then
	print -u2 "OSLL compilation failed with status $status"
	exit "$status"
fi
if [[ ! -f "$usbecm_ll" || ! -f "$adapter_ll" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_AUDIT_SESSION=1
export OSLL_AUDIT_SESSION protection diagnostics usbecm_ll adapter_ll
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
print "OSLL usbecm audit results: $result"

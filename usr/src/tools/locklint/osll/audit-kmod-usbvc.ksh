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
# evidence for usbvc both with and without the historical "one usbvc_state"
# declaration.  Databases are removed on exit and prior results are never
# replaced.
#

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
script="$script_dir/$(basename "$0")"
repo_root=$(CDPATH= cd "$script_dir/../../../../.." && pwd)
src="$repo_root/usr/src"
module_dir="$src/uts/intel/usbvc"
sunpro_bin=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
cc="$sunpro_bin/cc"
lock_lint="$sunpro_bin/lock_lint"
ssbd_include="$sunpro_bin/../prod/include/cc/ssbd"
roots='
usbvc.c:usbvc_open
usbvc.c:usbvc_close
usbvc.c:usbvc_read
usbvc.c:usbvc_ioctl
usbvc.c:usbvc_power
usbvc.c:usbvc_isoc_cb
usbvc.c:usbvc_isoc_exc_cb
usbvc.c:usbvc_disconnect_event_cb
usbvc.c:usbvc_reconnect_event_cb
usbvc.c:usbvc_info
usbvc.c:usbvc_attach
usbvc.c:usbvc_detach
usbvc.c:usbvc_devmap
'
functions='
_init
_fini
_info
usbvc_info
usbvc_attach
usbvc_detach
usbvc_cleanup
usbvc_open
usbvc_close
usbvc_read
usbvc_strategy
usbvc_minphys
usbvc_ioctl
usbvc_devmap
usbvc_power
usbvc_init_power_mgmt
usbvc_destroy_power_mgmt
usbvc_pm_busy_component
usbvc_pm_idle_component
usbvc_pwrlvl0
usbvc_pwrlvl1
usbvc_pwrlvl2
usbvc_pwrlvl3
usbvc_cpr_suspend
usbvc_resume_operation
usbvc_cpr_resume
usbvc_restore_device_state
usbvc_disconnect_event_cb
usbvc_reconnect_event_cb
usbvc_init_sync_objs
usbvc_fini_sync_objs
usbvc_init_lists
usbvc_fini_lists
usbvc_free_ctrl_descr
usbvc_free_stream_descr
usbvc_chk_descr_len
usbvc_parse_ctrl_if
usbvc_parse_stream_if
usbvc_parse_stream_ifs
usbvc_parse_color_still
usbvc_parse_frames
usbvc_parse_format_group
usbvc_parse_format_groups
usbvc_parse_stream_header
usbvc_alloc_read_bufs
usbvc_read_buf
usbvc_free_read_buf
usbvc_free_read_bufs
usbvc_alloc_map_bufs
usbvc_free_map_bufs
usbvc_open_isoc_pipe
usbvc_close_isoc_pipe
usbvc_start_isoc_polling
usbvc_isoc_cb
usbvc_isoc_exc_cb
usbvc_set_alt
usbvc_decode_stream_header
usbvc_serialize_access
usbvc_release_access
usbvc_vc_get_ctrl
usbvc_vc_set_ctrl
usbvc_vs_set_probe_commit
usbvc_vs_get_probe
usbvc_set_default_stream_fmt
usbvc_v4l2_ioctl
usbvc_v4l2_guid2fcc
usbvc_match_image_size
usbvc_v4l2_set_format
usbvc_v4l2_get_format
usbvc_v4l2_colorspace
usbvc_v4l2_query_buf
usbvc_v4l2_enqueue_buf
usbvc_v4l2_dequeue_buffer
usbvc_v4l2_match_ctrl
usbvc_v4l2_query_ctrl
usbvc_v4l2_get_ctrl
usbvc_v4l2_set_ctrl
usbvc_find_interval
usbvc_v4l2_set_parm
usbvc_v4l2_get_parm
'
expected_duplicates='function ystm.h:sum_overflows_u16 already loaded;
skipping this definition [systm.h,332]
function ystm.h:sum_overflows_hrtime already loaded;
skipping this definition [systm.h,338]
function ystm.h:sum_overflows_off already loaded;
skipping this definition [systm.h,345]'

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

cleanup_databases()
{
	rm -f "$usbvc_ll" "$v4l2_ll" "$adapter_ll"
}

run_load()
{
	file=$1
	name=$2
	allow_duplicates=$3

	output=$("$lock_lint" load "$file" 2>&1)
	status=$?
	print -r -- "$output" >"$diagnostics/$name.out"
	print -- "$status" >"$diagnostics/$name.status"
	if (( status == 0 )); then
		return 0
	fi
	if (( status == 2 )) && [[ "$allow_duplicates" == yes &&
	    "$output" == "$expected_duplicates" ]]; then
		return 0
	fi
	return "$status"
}

verify_coverage()
{
	for function in $functions
	do
		grep -q \
		    "^[^[:space:]]*:${function}[[:space:]].*\\[usbvc\\(_v4l2\\)\\{0,1\\}.c,[0-9][0-9]*\\]" \
		    "$diagnostics/functions.out" || return 1
	done
	! grep -q \
	    '\[usbvc\(_v4l2\)\{0,1\}.c,[0-9][0-9]*\].*=unanalyzed' \
	    "$diagnostics/functions.out"
}

write_summary()
{
	awk '
	    /^    variable = / {
		member = $3
		sub(/^usbvc_state::/, "", member)
	    }
	    /^       where = / {
		match($0, /\[usbvc(_v4l2)?\.c,[0-9]+\]/)
		if (RSTART != 0) {
			location = substr($0, RSTART + 1, RLENGTH - 2)
			sub(/,/, ":", location)
			print location, member
		}
	    }
	' "$diagnostics/analyze.out"
}

run_session()
{
	[[ -n "$LL_CONTEXT" ]] || exit 1
	cd "$repo_root" || exit 1

	run_load "$usbvc_ll" load-usbvc no || exit $?
	run_load "$v4l2_ll" load-v4l2 yes || exit $?
	run_load "$adapter_ll" load-adapter no || exit $?
	"$lock_lint" vars -a locklint_usbvc_annotation_probe::value \
	    >"$diagnostics/annotation.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/annotation.status"
	(( status == 0 )) || exit "$status"
	grep -q \
	    '^locklint_usbvc_annotation_probe::value[[:space:]].*assert=locklint_usbvc_annotation_probe::lock$' \
	    "$diagnostics/annotation.out" || exit 1
	"$lock_lint" vars -a usbvc_state::usbvc_dip \
	    >"$diagnostics/source-annotation.out" 2>&1
	status=$?
	print -- "$status" >"$diagnostics/source-annotation.status"
	(( status == 0 )) || exit "$status"
	grep -q \
	    '^usbvc_state::usbvc_dip[[:space:]].*assert=usbvc_state::usbvc_mutex$' \
	    "$diagnostics/source-annotation.out" || exit 1

	{
		if [[ "$OSLL_ONE_MODE" == with ]]; then
			"$lock_lint" declare one usbvc_state || exit $?
		fi
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
TMPDIR="$repo_root/$result/osll-protection/tmp"
usbvc_ll="$TMPDIR/usbvc.ll"
v4l2_ll="$TMPDIR/usbvc_v4l2.ll"
adapter_ll="$TMPDIR/check-kmod-usbvc-adapter.ll"
if [[ ! -x "$cc" || ! -x "$lock_lint" ||
    ! -d "$ssbd_include/sys" || ! -d "$module_dir" ]]; then
	print -u2 "required OSLL tool or usbvc input is unavailable"
	exit 1
fi
mkdir -p "$TMPDIR" || exit 1
cd "$module_dir" || exit 1
for database in usbvc.ll usbvc_v4l2.ll check-kmod-usbvc-adapter.ll
do
	if [[ -e "$database" ]]; then
		print -u2 "usbvc module directory already contains $database"
		exit 1
	fi
done

trap cleanup_databases EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

print "Compiling usbvc sources for OSLL"
compile_source ../../common/io/usb/clients/video/usbvc/usbvc.c usbvc.ll &&
    compile_source ../../common/io/usb/clients/video/usbvc/usbvc_v4l2.c \
    usbvc_v4l2.ll &&
    compile_source "$script_dir/check-kmod-usbvc-adapter.c" \
    check-kmod-usbvc-adapter.ll
status=$?
if (( status != 0 )) || [[ ! -f "$usbvc_ll" || ! -f "$v4l2_ll" ||
    ! -f "$adapter_ll" ]]; then
	print -u2 "OSLL compilation failed"
	exit 1
fi

for OSLL_ONE_MODE in with without
do
	protection="$repo_root/$result/osll-protection/$OSLL_ONE_MODE"
	diagnostics="$repo_root/$result/osll-diagnostics/$OSLL_ONE_MODE"
	reference="$diagnostics/accepted-reference.out"
	if [[ ! -f "$reference" ]]; then
		print -u2 "required input not found: $reference"
		exit 1
	fi
	for file in "$protection/session.out" "$protection/session.status" \
	    "$protection/session.ok" "$protection/vars-a.out" \
	    "$protection/vars-h.out" "$diagnostics/analyze.out" \
	    "$diagnostics/functions.out" "$diagnostics/report.out"
	do
		if [[ -e "$file" ]]; then
			print -u2 "refusing to overwrite result: $file"
			exit 1
		fi
	done
	OSLL_AUDIT_SESSION=1
	export OSLL_AUDIT_SESSION OSLL_ONE_MODE protection diagnostics
	export usbvc_ll v4l2_ll adapter_ll TMPDIR
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
done
print "OSLL usbvc audit results: $result"

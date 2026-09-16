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
# Run Solaris LockLint over the standalone usbvc kernel module.  Set
# OSLL_ONE_MODE to "with" or "without" to characterize the historical
# "one usbvc_state" declaration while keeping separate raw results.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-usbvc.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-usbvc.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbvc"
WORK_DIR="$MODULE_DIR/osll"
OSLL_ONE_MODE=${OSLL_ONE_MODE:-with}

case "$OSLL_ONE_MODE" in
with|without)
	;;
*)
	print -u2 "OSLL_ONE_MODE must be 'with' or 'without'"
	exit 1
	;;
esac

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbvc module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"
ANALYZE_RAW="$WORK_DIR/analyze.$OSLL_ONE_MODE.raw"
FUNCTIONS_RAW="$WORK_DIR/functions.$OSLL_ONE_MODE.raw"
SESSION_OK="$WORK_DIR/session.$OSLL_ONE_MODE.ok"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"

OSLL_HISTORICAL_ROOTS='
usbvc.c:usbvc_open
usbvc.c:usbvc_close
usbvc.c:usbvc_read
usbvc.c:usbvc_ioctl
usbvc.c:usbvc_power
usbvc.c:usbvc_isoc_cb
usbvc.c:usbvc_isoc_exc_cb
usbvc.c:usbvc_disconnect_event_cb
usbvc.c:usbvc_reconnect_event_cb
'

#
# Warlock's pseudo-kernel reached these entry points through dev_ops and
# cb_ops.  The focused adapter supplies the physio strategy/minphys edges.
#
OSLL_MODEL_ROOTS='
usbvc.c:usbvc_info
usbvc.c:usbvc_attach
usbvc.c:usbvc_detach
usbvc.c:usbvc_devmap
'

#
# The historical bus_ops mappings cannot affect this driver because its
# dev_ops contains no bus_ops vector.
#

OSLL_MODULE_FUNCTIONS='
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

#
# The second database repeats three inline definitions from systm.h.  Accept
# that known partial-load result, but do not hide any other skipped definition.
#
run_load()
{
	file=$1
	allow_duplicates=$2

	load_output=$("$LOCK_LINT" load "$file" 2>&1)
	status=$?
	case "$status" in
	0)
		if [[ -n "$load_output" ]]; then
			print -u2 -- "$load_output"
		fi
		return 0
		;;
	2)
		if [[ "$allow_duplicates" = yes &&
		    "$load_output" = "$OSLL_EXPECTED_DUPLICATES" ]]; then
			print -u2 -- "$load_output"
			return 0
		fi
		print -u2 -- "$load_output"
		return "$status"
		;;
	*)
		print -u2 -- "$load_output"
		return "$status"
		;;
	esac
}

verify_source_annotations()
{
	typeset output

	output=$("$LOCK_LINT" vars -a \
	    locklint_usbvc_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_usbvc_annotation_probe::value[[:space:]].*assert=locklint_usbvc_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the usbvc annotation probe"
		if [[ -n "$output" ]]; then
			print -u2 -- "$output"
		fi
		return 1
	fi

	output=$("$LOCK_LINT" vars -a usbvc_state::usbvc_dip 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^usbvc_state::usbvc_dip[[:space:]].*assert=usbvc_state::usbvc_mutex$'
	then
		print -u2 "OSLL did not retain usbvc source annotations"
		if [[ -n "$output" ]]; then
			print -u2 -- "$output"
		fi
		return 1
	fi
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $OSLL_MODULE_FUNCTIONS
	do
		if ! grep -q \
		    "^[^[:space:]]*:${function}[[:space:]].*\\[usbvc\\(_v4l2\\)\\{0,1\\}.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q \
	    '\[usbvc\(_v4l2\)\{0,1\}.c,[0-9][0-9]*\].*=unanalyzed' \
	    "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left usbvc module functions unanalyzed"
		return 1
	fi
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
	' "$ANALYZE_RAW"
}

run_session()
{
	typeset root

	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_USBVC_LL" || exit $?
	run_load "$OSLL_V4L2_LL" yes || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?

	if [[ "$OSLL_ONE_MODE" == with ]]; then
		run_quiet "$LOCK_LINT" declare one usbvc_state || exit $?
	fi

	for root in $OSLL_HISTORICAL_ROOTS $OSLL_MODEL_ROOTS
	do
		run_quiet "$LOCK_LINT" declare root "$root" || exit $?
	done

	"$LOCK_LINT" analyze >"$ANALYZE_RAW" 2>&1
	analyze_status=$?

	case "$analyze_status" in
	0|5)
		"$LOCK_LINT" funcs -o >"$FUNCTIONS_RAW" 2>&1
		coverage_status=$?
		if (( coverage_status != 0 )); then
			exit "$coverage_status"
		fi
		verify_module_coverage || exit 1
		write_summary >"$OUTPUT" || exit 1
		print ok >"$SESSION_OK" || exit 1
		exit 0
		;;
	*)
		print -u2 "lock_lint analyze failed with status $analyze_status"
		exit "$analyze_status"
		;;
	esac
}

if [[ "${OSLL_SESSION_MODE:-0}" == 1 ]]; then
	run_session
fi

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

for database in usbvc.ll usbvc_v4l2.ll
do
	if [[ -e "$database" ]]; then
		print -u2 "usbvc module directory already contains $database"
		print -u2 "rename or remove it before starting"
		exit 1
	fi
done

mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/usbvc.ll" \
    "$WORK_DIR/usbvc_v4l2.ll" "$WORK_DIR/check-kmod-usbvc-adapter.ll"

compile_source()
{
	source=$1
	database=$2

	"$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -D__lock_lint=1 \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$WORK_DIR/$database" || return 1
	fi
	return "$status"
}

print "Compiling usbvc sources for OSLL"
compile_source ../../common/io/usb/clients/video/usbvc/usbvc.c usbvc.ll &&
    compile_source ../../common/io/usb/clients/video/usbvc/usbvc_v4l2.c \
    usbvc_v4l2.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-usbvc-adapter.c" \
    check-kmod-usbvc-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_USBVC_LL="$WORK_DIR/usbvc.ll"
OSLL_V4L2_LL="$WORK_DIR/usbvc_v4l2.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-usbvc-adapter.ll"
if [[ ! -f "$OSLL_USBVC_LL" || ! -f "$OSLL_V4L2_LL" ||
    ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_ONE_MODE OSLL_SESSION_MODE OSLL_USBVC_LL OSLL_V4L2_LL \
    OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over usbvc ($OSLL_ONE_MODE one declaration)"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL usbvc session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL usbvc session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

if [[ ! -f "$REFERENCE" ]]; then
	print "OSLL usbvc baseline written to $OUTPUT."
	exit 0
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL usbvc semantic output matches check-kmod-usbvc.ref."
else
	print -u2 "OSLL usbvc semantic output differs from check-kmod-usbvc.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

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
# Run new locklint over both standalone usbvc translation units using their
# recorded production compiler arguments.  Require complete function coverage,
# all historical and pseudo-kernel roots, no unresolved indirect calls, the
# exact native diagnostic set, and one deliberate lock diagnostic.  Normalize
# the accepted multiline anchors before comparing source/member pairs with
# corrected OSLL.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/usbvc"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-usbvc.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-usbvc.out"
USBVC_SOURCE=../../common/io/usb/clients/video/usbvc/usbvc.c
USBVC_V4L2_SOURCE=../../common/io/usb/clients/video/usbvc/usbvc_v4l2.c

USBVC_FUNCTIONS='
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

HISTORICAL_ROOTS='
usbvc_open
usbvc_close
usbvc_read
usbvc_ioctl
usbvc_power
usbvc_isoc_cb
usbvc_isoc_exc_cb
usbvc_disconnect_event_cb
usbvc_reconnect_event_cb
'

PSEUDO_KERNEL_ROOTS='
usbvc_info
usbvc_attach
usbvc_detach
usbvc_devmap
usbvc_strategy
usbvc_minphys
'

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "usbvc module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi

mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$REFERENCE_FILTERED" "$OUTPUT"

run_locklint()
{
	"$LOCKLINT" "$@" \
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
	    -I../../intel -nostdinc -I../../common \
	    -mcmodel=kernel \
	    "$USBVC_SOURCE" "$USBVC_V4L2_SOURCE" \
	    "$SCRIPT_DIR/check-kmod-usbvc-sentinel.c"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $USBVC_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} .*usbvc\\(_v4l2\\)\\{0,1\\}.c:.*reachable=yes\$" \
		    "$ANALYZE_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "new locklint function inventory omitted:$missing"
		return 1
	fi
}

verify_automatic_roots()
{
	typeset function

	for function in $HISTORICAL_ROOTS $PSEUDO_KERNEL_ROOTS
	do
		if ! awk -v target="$function" '
		    /^function / {
			in_function = ($2 == target)
		    }
		    in_function && /^  root function-pointer-escape$/ {
			found = 1
		    }
		    END {
			exit !found
		    }
		' "$ANALYZE_RAW"; then
			print -u2 \
			    "new locklint did not infer usbvc root $function"
			return 1
		fi
	done
}

write_native_summary()
{
	awk -F "'" '
	    /usbvc.c:[0-9]+:[0-9]+: warning: locklint:/ &&
	    /\[unprotected-access\]$/ {
		match($0, /usbvc.c:[0-9]+/)
		location = substr($0, RSTART, RLENGTH)
		print location, $2
	    }
	' "$ANALYZE_RAW" | LC_ALL=C sort
}

write_reference_summary()
{
	awk '
	    $0 !~ /^#/ {
		if ($1 == "usbvc.c:433" && $2 == "usbvc_log_handle")
			$1 = "usbvc.c:431"
		else if ($1 == "usbvc.c:2034" && $2 == "usbvc_vc_header")
			$1 = "usbvc.c:2032"
		else if ($1 == "usbvc.c:2375" && $2 == "usbvc_curr_strm")
			$1 = "usbvc.c:2374"
		print
	    }
	' "$REFERENCE" | LC_ALL=C sort
}

print "Running new locklint over usbvc"
run_locklint --compat=osll --check-locks --dump-callgraph \
    --cf "$SCRIPT_DIR/usbvc.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint usbvc analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1

indirect_count=$(grep -c '^  call .* indirect$' "$ANALYZE_RAW")
if (( indirect_count != 0 )); then
	print -u2 "new locklint usbvc retained $indirect_count indirect " \
	    "calls instead of none"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

sentinel_count=$(grep -c \
    'check-kmod-usbvc-sentinel.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint usbvc analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

module_diagnostic_count=$(grep -c \
    'usbvc\(_v4l2\)\{0,1\}.c:.*warning: locklint:' "$ANALYZE_RAW")
unprotected_count=$(grep -c \
    'usbvc\(_v4l2\)\{0,1\}.c:.*warning: locklint:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( module_diagnostic_count != 7 || unprotected_count != 7 )); then
	print -u2 "Unexpected new locklint usbvc diagnostic counts:"
	print -u2 "  total=$module_diagnostic_count (expected 7)"
	print -u2 "  unprotected=$unprotected_count (expected 7)"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

if ! grep -q '^usbvc.c:433 usbvc_log_handle$' "$REFERENCE" ||
    ! grep -q '^usbvc.c:2034 usbvc_vc_header$' "$REFERENCE" ||
    ! grep -q '^usbvc.c:2375 usbvc_curr_strm$' "$REFERENCE"; then
	print -u2 "OSLL usbvc reference lacks expected reporting exceptions"
	exit 1
fi

write_native_summary >"$OUTPUT" || exit 1
write_reference_summary >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint usbvc covers every corrected OSLL problem" \
	    "with documented source-anchor exceptions."
else
	print -u2 "New locklint usbvc problem set differs from " \
	    "check-kmod-usbvc.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

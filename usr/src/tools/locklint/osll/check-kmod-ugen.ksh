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
# Run Solaris LockLint over the complete ugen kernel module.  The compiler
# arguments are the Sun-style inputs recorded in the ugen build's cw command,
# excluding options routed only to GCC or Smatch.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SCRIPT="$SCRIPT_DIR/$(basename "$0")"
REFERENCE="$SCRIPT_DIR/check-kmod-ugen.ref"
OUTPUT="$SCRIPT_DIR/check-kmod-ugen.out"
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
MODULE_DIR="$SRC/uts/intel/ugen"
WORK_DIR="$MODULE_DIR/osll"

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "ugen module directory not found: $MODULE_DIR"
	exit 1
fi
cd "$MODULE_DIR" || exit 1

SUNPRO_BIN=${SUNPRO_BIN:-/ws/onnv-tools/SUNWspro/SS12u1/bin}
CC="$SUNPRO_BIN/cc"
LOCK_LINT="$SUNPRO_BIN/lock_lint"
SSBD_INCLUDE="$SUNPRO_BIN/../prod/include/cc/ssbd"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
FUNCTIONS_RAW="$WORK_DIR/functions.raw"
SESSION_OK="$WORK_DIR/session.ok"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"

#
# These are the two roots in the historical ugen.wlcmd.
#
OSLL_HISTORICAL_ROOTS='
ugen.c:ugen_skel_disconnect_ev_cb
ugen.c:ugen_skel_reconnect_ev_cb
'

#
# The historical file also mapped seven bus_ops members to warlock_dummy.
# ugen's dev_ops has no bus_ops vector, and this focused session does not load
# unrelated bus implementations, so those mappings cannot affect its call
# graph and are intentionally omitted.
#

#
# Warlock's pseudo-kernel reached these DDI entry points through the module's
# dev_ops and cb_ops structures.  The focused comparison does not load that
# model, so declare the equivalent roots explicitly.
#
OSLL_MODEL_ROOTS='
ugen.c:ugen_skel_getinfo
ugen.c:ugen_skel_open
ugen.c:ugen_skel_close
ugen.c:ugen_skel_attach
ugen.c:ugen_skel_detach
ugen.c:ugen_skel_power
ugen.c:ugen_skel_read
ugen.c:ugen_skel_write
ugen.c:ugen_skel_poll
'

OSLL_MODULE_FUNCTIONS='
_init
_fini
_info
ugen_skel_getinfo
ugen_skel_open
ugen_skel_close
ugen_skel_attach
ugen_skel_detach
ugen_skel_power
ugen_skel_read
ugen_skel_write
ugen_skel_poll
ugen_skel_disconnect_ev_cb
ugen_skel_reconnect_ev_cb
'

run_quiet()
{
	"$@" >/dev/null
}

run_load()
{
	file=$1

	load_output=$("$LOCK_LINT" load "$file" 2>&1)
	status=$?
	if [[ -n "$load_output" ]]; then
		print -u2 -- "$load_output"
	fi
	return "$status"
}

#
# ugen has no source annotations.  Verify an adapter annotation instead, so a
# header-order error cannot silently turn this into an annotation-free run.
#
verify_source_annotations()
{
	typeset output

	output=$("$LOCK_LINT" vars -a \
	    locklint_ugen_annotation_probe::value 2>&1)
	status=$?
	if (( status != 0 )) ||
	    ! print -- "$output" | grep -q \
	    '^locklint_ugen_annotation_probe::value[[:space:]].*assert=locklint_ugen_annotation_probe::lock$'
	then
		print -u2 "OSLL did not retain the ugen annotation probe"
		if [[ -n "$output" ]]; then
			print -u2 -- "$output"
		fi
		return 1
	fi
}

#
# Require every function defined by ugen.c to appear in OSLL's function
# inventory and to have been analyzed.
#
verify_module_coverage()
{
	typeset function
	typeset missing=
	typeset symbol

	for function in $OSLL_MODULE_FUNCTIONS
	do
		case "$function" in
		_init|_fini|_info)
			symbol=":$function"
			;;
		*)
			symbol="ugen.c:$function"
			;;
		esac
		if ! grep -q \
		    "^${symbol}[[:space:]].*\\[ugen.c,[0-9][0-9]*\\]" \
		    "$FUNCTIONS_RAW"; then
			missing="$missing $function"
		fi
	done

	if [[ -n "$missing" ]]; then
		print -u2 "OSLL function inventory omitted:$missing"
		return 1
	fi

	if grep -q '\[ugen.c,[0-9][0-9]*\].*=unanalyzed' "$FUNCTIONS_RAW"
	then
		print -u2 "OSLL left ugen module functions unanalyzed"
		return 1
	fi
}

#
# Reduce OSLL output to actionable diagnostics.  Root guidance and calls to
# unmodeled external functions are outside this module's locking semantics.
#
write_summary()
{
	awk '
	    /^The following functions are clearly supposed to be roots/ {
		roots = 1
		next
	    }
	    roots && NF == 0 {
		roots = 0
		next
	    }
	    roots {
		next
	    }
	    /^\* Warning: calls made to the following function have not been analyzed\.$/ {
		external = 1
		next
	    }
	    external && /^  function = / {
		external = 0
		next
	    }
	    NF != 0 {
		print
	    }
	' "$ANALYZE_RAW"
}

#
# Load and configure ugen and its lifecycle adapter within one OSLL session.
#
run_session()
{
	if [[ -z "$LL_CONTEXT" ]]; then
		print -u2 "LL_CONTEXT is not set by lock_lint start"
		exit 1
	fi

	run_load "$OSLL_UGEN_LL" || exit $?
	run_load "$OSLL_ADAPTER_LL" || exit $?
	verify_source_annotations || exit $?

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

if [[ -e ugen.ll ]]; then
	print -u2 "ugen module directory already contains an OSLL database"
	print -u2 "rename or remove ugen.ll before starting"
	exit 1
fi
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$FUNCTIONS_RAW" "$OUTPUT" \
    "$SESSION_OK" "$REFERENCE_FILTERED" "$WORK_DIR/ugen.ll" \
    "$WORK_DIR/check-kmod-ugen-adapter.ll"

compile_source()
{
	source=$1
	database=$2

	"$CC" -Zll -m64 -Ui386 -U__i386 -xO3 -D_ASM_INLINES \
	    -xmodel=kernel -Wu,-save_args -v -g -xc99=%all \
	    -Wu,-save_args -errtags=yes \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 \
	    -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I"$SSBD_INCLUDE" -I"$SRC/uts/intel" -I"$SRC/uts/common" \
	    "$source"
	status=$?
	if [[ -f "$database" ]]; then
		mv "$database" "$WORK_DIR/$database" || return 1
	fi
	return "$status"
}

print "Compiling ugen sources for OSLL"
compile_source ../../common/io/usb/clients/ugen/ugen.c ugen.ll &&
    compile_source "$SCRIPT_DIR/check-kmod-ugen-adapter.c" \
    check-kmod-ugen-adapter.ll
compile_status=$?
if (( compile_status != 0 )); then
	print -u2 "OSLL compilation failed"
	exit "$compile_status"
fi

OSLL_UGEN_LL="$WORK_DIR/ugen.ll"
OSLL_ADAPTER_LL="$WORK_DIR/check-kmod-ugen-adapter.ll"
if [[ ! -f "$OSLL_UGEN_LL" || ! -f "$OSLL_ADAPTER_LL" ]]; then
	print -u2 "OSLL compilation did not create all expected databases"
	exit 1
fi

OSLL_SESSION_MODE=1
TMPDIR="$WORK_DIR"
export SRC OSLL_SESSION_MODE OSLL_UGEN_LL OSLL_ADAPTER_LL SESSION_OK TMPDIR

print "Running OSLL over ugen"
"$LOCK_LINT" start "$SCRIPT"
session_status=$?
if (( session_status != 0 )); then
	print -u2 "OSLL ugen session failed with status $session_status"
	print -u2 "Raw results: $WORK_DIR"
	exit "$session_status"
fi
if [[ ! -f "$SESSION_OK" ]]; then
	print -u2 "OSLL ugen session did not complete successfully"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "OSLL ugen semantic output matches check-kmod-ugen.ref."
else
	print -u2 "OSLL ugen semantic output differs from check-kmod-ugen.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw results: $WORK_DIR"
	exit 1
fi

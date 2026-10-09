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
# Run the new locklint over the complete ugen kernel module using the frontend
# arguments recorded in its cw command.  Require complete function coverage,
# automatic discovery of the historical callback roots, and one deliberate
# lock diagnostic before comparing ugen's findings with OSLL.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$(CDPATH= cd "$SCRIPT_DIR/../../.." && pwd)}
REPO_ROOT=$(CDPATH= cd "$SRC/../.." && pwd)
MODULE_DIR="$SRC/uts/intel/ugen"
WORK_DIR="$MODULE_DIR/newll"
LOCKLINT="$SRC/tools/locklint/locklint"
ANALYZE_RAW="$WORK_DIR/analyze.raw"
REFERENCE="$SCRIPT_DIR/../osll/check-kmod-ugen.ref"
REFERENCE_FILTERED="$WORK_DIR/reference.filtered"
OUTPUT="$SCRIPT_DIR/check-kmod-ugen.out"
PATCH="$SCRIPT_DIR/check-kmod-ugen.patch"
PATCH_CONFIG="$SCRIPT_DIR/check-kmod-ugen.cf"
PATCH_REFERENCE="$SCRIPT_DIR/check-kmod-ugen.ref"
PATCH_ANALYZE_RAW="$WORK_DIR/analyze-patched.raw"
PATCH_AUDIT_RAW="$WORK_DIR/audit-patched.raw"
PATCH_AUDIT_OUTPUT="$WORK_DIR/audit-patched.out"
PATCH_UNPROTECTED_RAW="$WORK_DIR/unprotected-patched.raw"
SENTINEL="$SCRIPT_DIR/check-kmod-sentinel.c"
PATCHED_FILES='
usr/src/uts/common/io/usb/clients/ugen/ugen.c
usr/src/uts/common/sys/usb/clients/ugen/ugend.h
'
UGEN_SOURCE=../../common/io/usb/clients/ugen/ugen.c
UGEN_FUNCTIONS='
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
HISTORICAL_ROOTS='
ugen_skel_disconnect_ev_cb
ugen_skel_reconnect_ev_cb
'

restore_sources()
{
	cd "$REPO_ROOT" || return 1
	git checkout -- $PATCHED_FILES
}

handle_exit()
{
	status=$?

	trap - EXIT HUP INT TERM
	if ! restore_sources; then
		print -u2 "failed to restore patched ugen source files"
		status=1
	fi
	exit "$status"
}

handle_signal()
{
	status=$1

	trap - EXIT HUP INT TERM
	if ! restore_sources; then
		print -u2 "failed to restore patched ugen source files"
		status=1
	fi
	exit "$status"
}

if [[ ! -d "$MODULE_DIR" ]]; then
	print -u2 "ugen module directory not found: $MODULE_DIR"
	exit 1
fi
if [[ ! -x "$LOCKLINT" ]]; then
	print -u2 "locklint not found: $LOCKLINT"
	exit 1
fi
for file in "$PATCH" "$PATCH_CONFIG" "$PATCH_REFERENCE" "$SENTINEL"
do
	if [[ ! -f "$file" ]]; then
		print -u2 "ugen comparison input not found: $file"
		exit 1
	fi
done

cd "$MODULE_DIR" || exit 1
mkdir -p "$WORK_DIR" || exit 1
rm -f "$ANALYZE_RAW" "$REFERENCE_FILTERED" "$OUTPUT" \
    "$PATCH_ANALYZE_RAW" "$PATCH_AUDIT_RAW" "$PATCH_AUDIT_OUTPUT" \
    "$PATCH_UNPROTECTED_RAW"

run_locklint()
{
	typeset sentinel=$1
	shift

	if [[ -n "$sentinel" ]]; then
		set -- "$@" "$sentinel"
	fi
	"$LOCKLINT" "$@" \
	    -D__sun -m64 -Ui386 -U__i386 -O2 \
	    -D_ASM_INLINES -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL \
	    -D_ELF64 -D_DDI_STRICT -Dsun -D__sun -D__SVR4 -DDEBUG \
	    -I../../intel -nostdinc -I../../common \
	    "$UGEN_SOURCE"
}

verify_module_coverage()
{
	typeset function
	typeset missing=

	for function in $UGEN_FUNCTIONS
	do
		if ! grep -q \
		    "^function ${function} tu=${UGEN_SOURCE} .*reachable=yes\$" \
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

	for function in $HISTORICAL_ROOTS
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
			    "new locklint did not infer historical root $function"
			return 1
		fi
	done
}

print "Running new locklint over ugen"
run_locklint "$SENTINEL" --compat=osll --dump-callgraph \
    --cf "$SCRIPT_DIR/ugen.cf" >"$ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint ugen analysis failed with status $status"
	print -u2 "see $ANALYZE_RAW"
	exit "$status"
fi

verify_module_coverage || exit 1
verify_automatic_roots || exit 1

sentinel_count=$(grep -c \
    'check-kmod-sentinel.c:.*warning:.*\[unprotected-access\]$' \
    "$ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint ugen analysis produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	print -u2 "see $ANALYZE_RAW"
	exit 1
fi

awk '/\[unprotected-access\]$/ &&
    $0 !~ /check-kmod-sentinel.c:/ { print }' \
    "$ANALYZE_RAW" >"$OUTPUT" || exit 1
awk '$0 !~ /^#/' "$REFERENCE" >"$REFERENCE_FILTERED" || exit 1

if cmp -s "$REFERENCE_FILTERED" "$OUTPUT"; then
	rm -f "$OUTPUT"
	print "New locklint ugen semantic output matches check-kmod-ugen.ref."
else
	print -u2 "New locklint ugen semantic output differs from " \
	    "check-kmod-ugen.ref:"
	diff -u "$REFERENCE_FILTERED" "$OUTPUT"
	print -u2 "Raw result: $ANALYZE_RAW"
	exit 1
fi

cd "$REPO_ROOT" || exit 1
dirty=$(git status --short -- $PATCHED_FILES) || exit 1
if [[ -n "$dirty" ]]; then
	print -u2 "refusing to patch modified ugen source files:"
	print -u2 -- "$dirty"
	exit 1
fi
git apply --check "$PATCH" || exit 1

trap handle_exit EXIT
trap 'handle_signal 129' HUP
trap 'handle_signal 130' INT
trap 'handle_signal 143' TERM
git apply "$PATCH" || exit 1

cd "$MODULE_DIR" || exit 1
print "Running new locklint over patched ugen"
run_locklint "$SENTINEL" --compat=osll --cf "$PATCH_CONFIG" \
    >"$PATCH_ANALYZE_RAW" 2>&1
status=$?
if (( status != 0 )); then
	print -u2 "new locklint patched ugen analysis failed with status $status"
	print -u2 "see $PATCH_ANALYZE_RAW"
	exit "$status"
fi

sentinel_count=$(grep -c \
    'check-kmod-sentinel.c:.*warning:.*\[unprotected-access\]$' \
    "$PATCH_ANALYZE_RAW")
if (( sentinel_count != 1 )); then
	print -u2 "new locklint patched ugen produced $sentinel_count " \
	    "sentinel diagnostics instead of one"
	exit 1
fi
if grep -q '\[read-only-data-modified\]$' "$PATCH_ANALYZE_RAW"; then
	print -u2 "new locklint rejected private ugen initialization"
	exit 1
fi

run_locklint "" --compat=osll --no-diagnostics \
    --cf "$PATCH_CONFIG" --audit-protection="$PATCH_AUDIT_RAW" \
    >/dev/null 2>&1 || exit 1
awk -F '\t' '
    /^DATUM\t/ {
	report = 1
	next
    }
    report && NF == 4 {
	datum = $1
	sub(/^.*\/ugen.c::/, "ugen.c::", datum)
	print datum "\t" $2 "\t" $3 "\t" $4
    }
' "$PATCH_AUDIT_RAW" >"$PATCH_AUDIT_OUTPUT" || exit 1
if ! cmp -s "$PATCH_REFERENCE" "$PATCH_AUDIT_OUTPUT"; then
	print -u2 "new locklint patched ugen protection audit differs:"
	diff -u "$PATCH_REFERENCE" "$PATCH_AUDIT_OUTPUT"
	exit 1
fi

run_locklint "" --compat=osll --no-diagnostics \
    --cf "$PATCH_CONFIG" --audit-unprotected="$PATCH_UNPROTECTED_RAW" \
    >/dev/null 2>&1 || exit 1
line_count=$(wc -l <"$PATCH_UNPROTECTED_RAW")
if (( line_count != 1 )) ||
    ! grep -q '^Unprotected audit:' "$PATCH_UNPROTECTED_RAW"; then
	print -u2 "new locklint patched ugen has unprotected audit entries"
	exit 1
fi

print "New locklint patched ugen protection audit matches."

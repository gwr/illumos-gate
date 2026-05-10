#
# xyzzy2d.awk - awk program for *.d file generation
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
# Copyright 2026 Gordon W. Ross
#

#
# Usage: nawk -f xyzzy2d.awk file.gen.i file.d.in > file.d
#
# Before this program runs, the makefile creates file.gen.i using
#	 "$(CC) ... -o file.gen.i -E file.gen.c"
#
# Then make runs this with the two files: file.gen.i file.d.in
#
# This parses the "NAME"=value macro expansions in file.gen.i
# and then substitutes @NAME@ patterns in file.d.in to create
# the finished file.d program on standard output.
#
# Note file.gen.i can have cpp "linemarkers" and these may appear
# within the expansion of the macros in file.gen.c so this has to
# reassemble any multi-line macro expansions that may appear.
# The .gen.i file uses XYZZY_BEGIN / XYZZY_END markers to simplify
# the job of finding what we need in the cpp output.
#
# There's one special case in the template substitution for the
# sysevent.d.in file. That uses: stringof(@SE_CLASS_NAME@(ev))
# and a SED_MACRO that expands to "x" = x(ev) so the template
# would be expanded to: stringof(expression-using(ev))(ev).
# The substitution removes the "(ev)" suffix for that case.
#

BEGIN {
	if (ARGC < 3)
		print "Usage: nawk -f xyzzy2d.awk file.gen.i file.d.in" \
		    > "/dev/stderr"
	in_xyzzy = 0
}

# Runs once at the start of each input file.
FNR == 1 {
	firstfile = (NR == FNR)
	strip_ev  = (FILENAME ~ /sysevent\.d\.in$/)
	if (!firstfile && in_xyzzy) {
		print FILENAME ": Missing XYZZY_END (name=" name ")" \
		    > "/dev/stderr"
		in_xyzzy = 0
		buf = ""
	}
}

# Store name=value pair from an XYZZY block into the dictionary.
function add_dict() {
	gsub(/ /, "", buf)
	if (name == "")
		print FILENAME ":" FNR ": XYZZY block with no name" \
		    > "/dev/stderr"
	subs[name] = buf
}

# Skip cpp line-number markers (e.g. "# 123 "file.h" 1") in the cpp output.
/^# [0-9]/ {
	if (firstfile)
		next
}

# Single-line case: XYZZY_BEGIN "NAME" = value XYZZY_END all on one line.
/XYZZY_BEGIN .* XYZZY_END/ {
	if (firstfile) {
		name = $2; gsub(/"/, "", name)
		idx = index($0, "=")
		rest = substr($0, idx + 1)
		buf = substr(rest, 1, index(rest, "XYZZY_END") - 1)
		add_dict()
		next
	}
}

# Start of a possibly multi-line XYZZY block.
/XYZZY_BEGIN/ {
	if (firstfile) {
		if (in_xyzzy) {
			print FILENAME ":" FNR ": Nested XYZZY_BEGIN" \
			    > "/dev/stderr"
		}
		name = $2; gsub(/"/, "", name)
		idx = index($0, "=")
		buf = (idx > 0) ? substr($0, idx + 1) : ""
		in_xyzzy = 1
		next
	}
}

# End of a multi-line XYZZY block.
/XYZZY_END/ {
	if (firstfile) {
		if (in_xyzzy) {
			add_dict()
			in_xyzzy = 0
		} else {
			print FILENAME ":" FNR ": Unexpected XYZZY_END" \
			    > "/dev/stderr"
		}
		next
	}
}

# Processing that happens for all lines (unless above did "next").
# For the first file: accumulate lines within a multi-line XYZZY block.
# For the second file: substitute @NAME@ tokens in the .d.in template.
{
	if (firstfile) {
		if (in_xyzzy)
			buf = buf $0
		next
	}
	line = $0
	out = ""
	while (match(line, /@[A-Z_][A-Z0-9_]*@/)) {
		token = substr(line, RSTART + 1, RLENGTH - 2)
		if (!(token in subs)) {
			print FILENAME ":" NR ": undefined token: @" token "@" \
			    > "/dev/stderr"
			exit 1
		}
		suffix = substr(line, RSTART + RLENGTH)
		# sysevent.d.in has @TOKEN@(ev) — strip the (ev) since the
		# substituted value already contains the (ev) argument.
		if (strip_ev && substr(suffix, 1, 4) == "(ev)")
			suffix = substr(suffix, 5)
		out = out substr(line, 1, RSTART - 1) subs[token]
		line = suffix
	}
	print out line
}

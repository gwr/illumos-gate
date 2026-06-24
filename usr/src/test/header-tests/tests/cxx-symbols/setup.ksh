#!/usr/bin/ksh
#
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
# Copyright 2014 Garrett D'Amore <garrett@damore.org>
# Copyright 2026 Gordon W. Ross
#

export STF_SUITE=/opt/header-tests

# First we set $dir to dirname $0, using efficient ksh builtins.
case $0 in
*/*)
	dir=${0%/*}
	prog=${0##*/}
	;;
*)
	dir=.
	prog=${0}
	;;
esac

cfg=cxx-symbols/${prog%.ksh}.cfg

if [[ ! -f ${cfg} && $cfg == cxx-symbols/setup.cfg ]]
then
	# compiler check only
	cfg=-C
fi

pdir=$dir/../common
prog=cxx_symbols_test

for arg in $*
do
	if [[ $arg == "-d" ]]
	then
		debug=yes
	fi
done

# Run the architecture specific versions of the program,
# either ..._64 or ..._32 or both, depending on isainfo
# If we don't find any program, be sure to error out.
found=
for isa in $(/usr/bin/isainfo)
do
	case $isa in
	amd64|sparcv9)	sfx=_64 ;;
	i386|sparc)	sfx=_32 ;;
	aarch64)	sfx=_64 ;;
	*)
		[[ -n $debug ]] && print "Skipping unknown ISA: $isa"
		continue
		;;
	esac

	p=${pdir}/${prog}${sfx}
	[[ -n $debug ]] && print "Executing $p $* ${cfg}"
	[[ -f $p ]] || { print "ERROR: $p not found" >&2; exit 1; }
	found=yes
	$p $* ${cfg} || exit 1
done
if [[ -z $found ]]; then
	print "ERROR: no known ISA found in isainfo output" >&2
	exit 1
fi
exit 0

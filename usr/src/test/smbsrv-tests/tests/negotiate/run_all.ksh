#!/usr/bin/ksh

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
# Copyright 2022 Tintri by DDN, Inc. All rights reserved.
#

export SMBSRV_TESTS=${SMBSRV_TESTS:-/opt/smbsrv-tests}
export NEGO_TESTDIR=$SMBSRV_TESTS/tests/negotiate

SKIP_PROGS="$0 ctx_common"

ARGS=
while getopts c: c; do
	case $c in
	'c')
		cfgfile=$OPTARG
		[[ -f $cfgfile ]] || fail "Cannot read file: $cfgfile"
		ARGS=$ARGS"-c $cfgfile "
		;;
	esac
done
shift $((OPTIND - 1))

MYRES=0
for TEST in $NEGO_TESTDIR/*; do
	if [ -z "$(echo $SKIP_PROGS | grep $(basename $TEST))" ]; then
		$TEST $ARGS
		RES=$?
		[ $RES != 0 ] && MYRES=$RES
	fi
done

exit $MYRES

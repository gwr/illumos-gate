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
# Copyright 2021 Tintri by DDN, Inc. All rights reserved.
#

export SMBSRV_TESTS="/opt/smbsrv-tests"

runsmbtor=$SMBSRV_TESTS/bin/run_smbtorture
excl_file=$SMBSRV_TESTS/include/smbtor-excl-rpc.txt

cfgfile=${CFGFILE:-$SMBSRV_TESTS/include/default.cfg}
outdir=${OUTDIR:-/var/tmp/test_results/smbsrv-tests}
basefile=$BASEFILE

function fail
{
	echo $1
	exit ${2:-1}
}

while getopts b:c:o:t: c; do
	case $c in
	'b')
		basefile=$OPTARG
		;;
	'c')
		cfgfile=$OPTARG
		[[ -f $cfgfile ]] || fail "Cannot read file: $cfgfile"
		;;
	'o')
		outdir=$OPTARG
		;;
	't')
		timeout="-t $OPTARG"
		;;
	esac
done
shift $((OPTIND - 1))

. $cfgfile

export PATH="$(dirname $SMBTOR):$PATH"

mkdir -p $outdir
cd $outdir || fail "Could not cd to $outdir"

tstamp=$(date +'%Y%m%dT%H%M%S')
logfile=./smbtor-rpc-${tstamp}.log
outfile=./smbtor-rpc-${tstamp}.summary
outbase=${basefile:-./smbtor-rpc-baseline.summary}

if [[ -z "$timeout" && -n "$TIMEOUT" ]]; then
	timeout="-t $TIMEOUT"
fi

#It would be nice to have a generic 'Can I connect to this server/share?' test
$SMBTOR -U "$SMBT_USER%${SMBT_PASS}" //$SMBT_HOST/IPC\$	\
    rpc.wkssvc.wkssvc.NetWkstaGetInfo || \
    fail "Cannot connect to //$SMBT_HOST/IPC\$"

echo "Running smbtorture/RPC tests with //$SMBT_HOST/IPC\$"
$runsmbtor -m rpc -e $excl_file -o $logfile $timeout \
    "$SMBT_HOST" "IPC\$" "$SMBT_USER" "${SMBT_PASS}" |
     tee $outfile

if [ -f $outbase ] ; then
	echo "Comparing with baseline"
	diff $outbase $outfile
fi

exit 0

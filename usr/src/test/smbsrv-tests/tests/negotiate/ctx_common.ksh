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

ctxtest=$SMBSRV_TESTS/bin/smb311_contexts

cfgfile=${CFGFILE:-$SMBSRV_TESTS/include/default.cfg}
outdir=${OUTDIR:-/var/tmp/test_results/smbsrv-tests}

TESTNAME=$(basename $0)

function fail
{
	echo $1
	echo "test $TESTNAME failed"
	exit ${2:-1}
}

function run_test
{
	if [ -n "$ENCRYPT_ALGS" ] ; then
		RUN_ALGS="$ENCRYPT_ALGS "
	fi

	if [ -n "$PREAUTH_ALGS" ] ; then
		RUN_ALGS=$RUN_ALGS"$PREAUTH_ALGS "
	fi

	echo "Running $ctxtest $SMBT_HOST $NUM_ENCRYPT $NUM_PREAUTH $RUN_ALGS"
	OUTPUT=$($ctxtest $SMBT_HOST $NUM_ENCRYPT $NUM_PREAUTH $RUN_ALGS)
	echo "$OUTPUT"

	STATUS=$(echo "$OUTPUT" | sed -n 's/^status: //p')
	if [ "$STATUS" != "$EXPECTED_STATUS" ]; then
		fail "Wrong status: $STATUS (expected $EXPECTED_STATUS)"
	fi

	ALG=$(echo "$OUTPUT" |  sed -n 's/^preauth alg: 0x//p')
	if [ "$ALG" != "$EXPECTED_PREAUTH_ALG" ]; then
		ALG=${ALG:-"None"}
		EXPECTED_ALG=${EXPECTED_PREAUTH_ALG:-"None"}
		if [ -z "$EXPECTED_PREAUTH_ALG" ]; then
			fail "Unexpected preauth context (got $ALG)"
		else
			fail "Wrong preauth algorithm: $ALG " \
			    "(expected $EXPECTED_ALG)"
		fi
	fi

	ALG=$(echo "$OUTPUT" |  sed -n 's/^encrypt alg: 0x//p')
	if [ "$ALG" != "$EXPECTED_ENCRYPT_ALG" ]; then
		ALG=${ALG:-"None"}
		EXPECTED_ALG=${EXPECTED_ENCRYPT_ALG:-"None"}
		if [ -z "$EXPECTED_ENCRYPT_ALG" ]; then
			fail "Unexpected encryption context (got $ALG)"
		else
			fail "Wrong encryption algorithm: $ALG " \
			    "(expected $EXPECTED_ALG)"
		fi
	fi

	echo "test $TESTNAME passed"
	exit 0
}

while getopts c: c; do
	case $c in
	'c')
		cfgfile=$OPTARG
		[[ -f $cfgfile ]] || fail "Cannot read file: $cfgfile"
		;;
	esac
done
shift $((OPTIND - 1))

. $cfgfile


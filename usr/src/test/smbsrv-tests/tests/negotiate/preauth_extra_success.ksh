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

. $SMBSRV_TESTS/tests/negotiate/ctx_common

NUM_ENCRYPT=0
NUM_PREAUTH=10
PREAUTH_ALGS="100 101 1 103 104 105 106 107 108 109"
ENCRYPT_ALGS=

EXPECTED_STATUS="0"
EXPECTED_PREAUTH_ALG=1
EXPECTED_ENCRYPT_ALG=

run_test

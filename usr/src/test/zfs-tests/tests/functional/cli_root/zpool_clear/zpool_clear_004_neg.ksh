#!/bin/ksh -p
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
# Copyright 2026 RackTop Systems Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_clear/zpool_clear.cfg

#
# DESCRIPTION:
# A RAID-Z vdev name with an explicit parity level must match the vdev.
#
# STRATEGY:
# 1. Create a RAID-Z2 pool.
# 2. Verify the generic and matching RAID-Z2 names identify the vdev.
# 3. Verify names with other or invalid parity levels are rejected.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && log_must zpool destroy -f $TESTPOOL1
	rm -f $TESTDIR/raidz-clear.*
}

log_assert "RAID-Z vdev names require the correct parity level."
log_onexit cleanup

for i in 0 1 2; do
	log_must mkfile $FILESIZE $TESTDIR/raidz-clear.$i
done

log_must zpool create $TESTPOOL1 raidz2 \
    $TESTDIR/raidz-clear.0 $TESTDIR/raidz-clear.1 $TESTDIR/raidz-clear.2

log_must zpool clear $TESTPOOL1 raidz-0
log_must zpool clear $TESTPOOL1 raidz2-0
log_mustnot zpool clear $TESTPOOL1 raidz1-0
log_mustnot zpool clear $TESTPOOL1 raidz3-0
log_mustnot zpool clear $TESTPOOL1 raidz9-0

log_pass "RAID-Z vdev names enforce the configured parity level."

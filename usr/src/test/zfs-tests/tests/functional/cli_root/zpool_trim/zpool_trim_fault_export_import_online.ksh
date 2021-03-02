#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may not use this file except in compliance with the License.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2021 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# A faulted device does not resume an interrupted trim.
#
# STRATEGY:
# 1. Start trimming one side of a mirror.
# 2. Fault the device, export and import the pool, then bring it online.
# 3. Verify that trimming was canceled and does not restart.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"
DISK2="$(echo $DISKS | cut -d' ' -f2)"

log_must zpool create -f $TESTPOOL mirror $DISK1 $DISK2

log_must zpool trim -r 128M $TESTPOOL $DISK1
progress="$(trim_progress $TESTPOOL $DISK1)"
[[ -z "$progress" ]] && log_fail "Trimming did not start"

log_must zpool offline -f $TESTPOOL $DISK1
log_must check_vdev_state $TESTPOOL $DISK1 "FAULTED"
log_must eval "zpool status -t $TESTPOOL | grep $DISK1 | grep untrimmed"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must check_vdev_state $TESTPOOL $DISK1 "FAULTED"
log_must eval "zpool status -t $TESTPOOL | grep $DISK1 | grep unsupported"

log_must zpool online $TESTPOOL $DISK1
log_must zpool clear $TESTPOOL $DISK1
log_must check_vdev_state $TESTPOOL $DISK1 "ONLINE"
log_must eval "zpool status -t $TESTPOOL | grep $DISK1 | grep untrimmed"

log_pass "Faulted trim remains canceled"

#!/usr/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2014 by Delphix. All rights reserved.
# Copyright 2026 RackTop Systems Inc.
#

. $STF_SUITE/include/libtest.shlib

log_must set_tunable32 zfs_removal_suspend_progress 0
default_cleanup

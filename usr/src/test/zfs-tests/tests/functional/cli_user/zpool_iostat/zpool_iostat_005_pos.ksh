#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the License at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2016-2017 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

export ZPOOL_SCRIPTS_AS_ROOT=1
export ZPOOL_SCRIPTS_PATH="$ZPOOLSCRIPTDIR"

log_assert "zpool iostat -c runs the bundled upath script"
test_zpool_script upath "$TESTPOOL" "zpool iostat -Pv -c"
log_must eval "zpool iostat -c | grep -q upath"

log_pass "zpool iostat -c runs the bundled upath script"

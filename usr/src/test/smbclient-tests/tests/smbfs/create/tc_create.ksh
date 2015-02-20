#!/usr/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
#

#
# create test case
#

. ${CTI_SUITE}/config/config

ic1="create001"
ic2="create002"
ic3="create003"
ic4="create004"
ic5="create005"
ic6="create006"
ic7="create007"
ic8="create008"
ic9="create009"
ic10="create010"
ic11="create011"
ic12="create012"

iclist="$ic1 $ic2 $ic3 $ic4 $ic5 $ic6 $ic7 $ic8"
test_list="$iclist $ic9 $ic10 $ic11 $ic12"

. ${CTI_SUITE}/tests/smbfs/create/tp_create_001
. ${CTI_SUITE}/tests/smbfs/create/tp_create_002
. ${CTI_SUITE}/tests/smbfs/create/tp_create_003
. ${CTI_SUITE}/tests/smbfs/create/tp_create_004
. ${CTI_SUITE}/tests/smbfs/create/tp_create_005
. ${CTI_SUITE}/tests/smbfs/create/tp_create_006
. ${CTI_SUITE}/tests/smbfs/create/tp_create_007
. ${CTI_SUITE}/tests/smbfs/create/tp_create_008
. ${CTI_SUITE}/tests/smbfs/create/tp_create_009
. ${CTI_SUITE}/tests/smbfs/create/tp_create_010
. ${CTI_SUITE}/tests/smbfs/create/tp_create_011
. ${CTI_SUITE}/tests/smbfs/create/tp_create_012

. ${CTI_SUITE}/include/services_common.ksh
. ${CTI_SUITE}/include/smbutil_common.ksh
. ${CTI_SUITE}/include/utils_common.ksh
. ${CTI_SUITE}/include/smbmount_common.ksh

. ${CTI_ROOT:?}/lib/ctilib.ksh

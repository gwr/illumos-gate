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
# Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
#

#
# ID: smbutil_012
#
# DESCRIPTION:
#        Verify smbutil -A can work
#
# STRATEGY:
#	1. run "smbutil view -A //$server"
#	2. smbutil can get share information
#

smbutil012() {
tet_result PASS

tc_id="smbutil012"
tc_desc="Verify smbutil -A can work"
print_test_case $tc_id - $tc_desc

if [[ $STC_CIFS_CLIENT_DEBUG == 1 ]] || \
	[[ *:${STC_CIFS_CLIENT_DEBUG}:* == *:$tc_id:* ]]; then
    set -x
fi

server=$(server_name) || return

cmd="smbutil view -A //$server"
cti_execute_cmd $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: smbutil can't view the shares"
	return
else
	cti_report "PASS: smbutil can view the shares"
fi

parse_view_output public cti_stdout
if [[ $? != 0 ]]; then
	cti_fail "FAIL: smbutil view can't get the public share"
	return
else
	cti_report "PASS: smbutil view can get the public share"
fi

cti_pass "${tc_id}: PASS"
}

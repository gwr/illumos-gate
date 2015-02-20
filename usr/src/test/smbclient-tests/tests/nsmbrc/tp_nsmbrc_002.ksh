#!/bin/ksh -p
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
# ID:nsmbrc002
#
# DESCRIPTION:
#        Verify password can work
#
# STRATEGY:
#       1. create a .nsmbrc file
#       2. run "smbutil view //$TUSER@server"
#       3. password works fine
#

. $STF_SUITE/include/libtest.ksh

tc_id="nsmbrc002"
tc_desc="Verify password can work in nsmbrc"
print_test_case $tc_id" - "$tc_desc

if [[ $STC_CIFS_CLIENT_DEBUG == 1 ]] || \
	[[ *:${STC_CIFS_CLIENT_DEBUG}:* == *:$tc_id:* ]]; then
    set -x
fi

server=$(server_name) || return

rm -f ~root/.nsmbrc
pass=$(smbutil crypt $TPASS)
echo "[default]" > ~root/.nsmbrc
echo "password=$pass" >> ~root/.nsmbrc
chmod 600 ~root/.nsmbrc

cmd="smbutil view //$TUSER@$server"
cti_execute -i '' FAIL $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: password property in default section doesn't work"
	return
else
	cti_report "PASS: password property in default section works"
fi


SERVER=$(echo $server | tr "[:lower:]" "[:upper:]")
echo "[$SERVER]" > ~root/.nsmbrc
echo "addr=$server" >> ~root/.nsmbrc
echo "password=$pass" >> ~root/.nsmbrc
chmod 600 ~root/.nsmbrc

cmd="smbutil view //$TUSER@$server"
cti_execute -i '' FAIL $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: password property in SERVER section doesn't work"
	return
else
	cti_report "PASS: password property in SERVER section works"
fi

cmd="smbutil view //$TUSER1@$server"
cti_execute -i '' FAIL $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: password property doesn't work for user '$TUSER1'"
	return
else
	cti_report "PASS: password property works for user '$TUSER1'"
fi

rm -f ~root/.nsmbrc

cti_pass "${tc_id}: PASS"

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
# ID: smbmount_013
#
# DESCRIPTION:
#	 -o uid work well
#
# STRATEGY:
#        1. run "mount -F smbfs -o uid=xxx //$TUSER:$TPASS@...
#        2. ls -ld /mnt get owner xxx
#

smbmount013() {
tet_result PASS

tc_id="smbmount013"
tc_desc="uid=xxx worked well"
print_test_case $tc_id - $tc_desc

if [[ $STC_CIFS_CLIENT_DEBUG == 1 ]] || \
	[[ *:${STC_CIFS_CLIENT_DEBUG}:* == *:$tc_id:* ]]; then
    set -x
fi

server=$(server_name) || return

testdir_init $TDIR
smbmount_clean $TMNT
smbmount_init $TMNT

# This tests a feature not exposed with ACLs.
smbmount_enable_noacl
if [[ $? != 0 ]]; then
	smbmount_clean $TMNT
	cti_unsupported "UNSUPPORTED (requires noacl)"
	return
fi

# XXX: Should get this user from config
tc_uid="smmsp"

cmd="mount -F smbfs -o noacl,uid=$tc_uid \
 //$TUSER:$TPASS@$server/public $TMNT"
cti_execute -i '' FAIL $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: $cmd"
	return
fi

usr=$(ls -ld $TMNT|awk '{ print $3}')

if [[ $usr != "$tc_uid" ]]; then
	cti_fail "FAIL: ls -ld, expected $tc_uid, got $usr"
	cti_execute_cmd "cd -"
	smbmount_clean $TMNT
	return
fi

cti_execute_cmd "cd $TMNT"

cti_execute_cmd "touch a"
usr=$(ls -l a|awk '{ print $3}')
if [[ $usr != "$tc_uid" ]]; then
	cti_fail "FAIL: touch a, expected $tc_uid usr, got $usr"
	cti_execute_cmd "cd -"
	smbmount_clean $TMNT
	return
fi

cti_execute_cmd "rm -rf b"
cti_execute_cmd "mkdir b"
usr=$(ls -ld b|awk '{ print $3}')
if [[ $usr != "$tc_uid" ]]; then
	cti_fail "FAIL: mkdir b, expected $tc_uid usr, got $usr"
	cti_execute_cmd "cd -"
	smbmount_clean $TMNT
	return
fi

cti_execute_cmd "rm -rf *"
cti_execute_cmd "cd -"

cmd="umount $TMNT"
cti_execute_cmd $cmd
if [[ $? != 0 ]]; then
	cti_fail "FAIL: failed to umount the $TMNT"
	return
fi

smbmount_clean $TMNT

cti_pass "${tc_id}: PASS"
}

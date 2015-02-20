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
# mmap test purpose
#
# __stc_assertion_start
#
# ID: mmap_008
#
# DESCRIPTION:
#        Verify smbfs will keep the SMB FID alive when
#	 there are mapped pages associated with it.
#
# STRATEGY:
#       1. run "mount -F smbfs //server/public /export/mnt"
#       2. mkfile in smbfs
#	3. open, mmap & close the file, then write some data into the mapped addr
#	4. verify the data successfully written back to smb server
# KEYWORDS:
#
# TESTABILITY: explicit
#
# __stc_assertion_end
#

. $STF_SUITE/include/libtest.ksh

tc_id="mmap008"
tc_desc=" Verify smbfs will keep the SMB FID alive when there are mapped pages associated with it"
print_test_case $tc_id - $tc_desc

if [[ $STC_CIFS_CLIENT_DEBUG == 1 ]] || \
	[[ *:${STC_CIFS_CLIENT_DEBUG}:* == *:$tc_id:* ]]; then
    set -x
fi 

server=$(server_name) || return 

testdir=$TDIR
mnt_point=$TMNT

testdir_init $testdir
smbmount_clean $mnt_point
smbmount_init $mnt_point

test_file="tmp008"

cmd="mount -F smbfs //$TUSER:$TPASS@$server/public $mnt_point"
cti_execute -i '' FAIL $cmd
if (($?!=0)); then
	cti_fail "FAIL: smbmount can't mount the public share"
	return
else
	cti_report "PASS: smbmount can mount the public share"
fi


cti_execute_cmd "cd $mnt_point"

# open, mmap & close a file in smbfs, then write something into it
cti_execute_cmd "close_wr ${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: close_wr ${test_file} failed"
	return
else
	cti_report "PASS: close_wr ${test_file} succeeded"
fi

# do the same thing in local file, for comparison
cti_execute_cmd "close_wr ${testdir}/${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: close_wr ${testdir}/${test_file} failed"
	return
else
	cti_report "PASS: close_wr ${testdir}/${test_file} succeeded"
fi


cti_execute FAIL "sum ${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: smbfs sum failed"
	return
else
	cti_report "PASS: smbfs sum succeeded"
fi
read sum1 cnt1 junk < cti_stdout

cti_execute FAIL "sum ${testdir}/${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: local sum failed"
	return
else
	cti_report "PASS: local sum succeeded"
fi
read sum2 cnt2 junk < cti_stdout

if [[ $sum1 != $sum2 ]] ; then
        cti_fail "FAIL: the files are different"
        return
else
        cti_report "PASS: the files are the same"
fi

cti_execute_cmd "rm -rf $testdir/*"
cti_execute_cmd "rm -f ${test_file}"
cti_execute_cmd "cd -"

smbmount_clean $mnt_point

cti_pass "${tc_id}: PASS"

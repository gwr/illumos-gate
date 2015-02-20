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
# ID: mmap_006
#
# DESCRIPTION:
#        Verify compatibility between open(O_RDWR) & mmap(PROT_READ|PROT_WRITE, MAP_SHARED)
#
# STRATEGY:
#       1. run "mount -F smbfs //server/public /export/mnt"
#       2. mkfile in smbfs & local dir, with the same size
#	3. open(O_RDWR) & mmap(PROT_READ|PROT_WRITE, MAP_SHARED) the smbfs file
#	4. read data from the local file and write into the smbfs file
#	4. diff the 2 files
# KEYWORDS:
#
# TESTABILITY: explicit
#
# __stc_assertion_end
#

. $STF_SUITE/include/libtest.ksh

tc_id="mmap006"
tc_desc=" Verify compatibility between open(O_RDWR) & mmap(PROT_READ|PROT_WRITE, MAP_SHARED)"
print_test_case $tc_id - $tc_desc

if [[ $STC_CIFS_CLIENT_DEBUG == 1 ]] || \
	[[ *:${STC_CIFS_CLIENT_DEBUG}:* == *:$tc_id:* ]]; then
    set -x
fi 

size=1111k

server=$(server_name) || return 

testdir=$TDIR
mnt_point=$TMNT

testdir_init $testdir
smbmount_clean $mnt_point
smbmount_init $mnt_point

test_file="tmp006"

cmd="mount -F smbfs //$TUSER:$TPASS@$server/public $mnt_point"
cti_execute -i '' FAIL $cmd
if (($?!=0)); then
	cti_fail "FAIL: smbmount can't mount the public share"
	return
else
	cti_report "PASS: smbmount can mount the public share"
fi

cti_execute_cmd "cd $mnt_point"

# make a smbfs file
cti_execute_cmd "mkfile_mmap -n $size -f ${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: mkfile_mmap -n $size -f ${test_file} failed"
	return
else
	cti_report "PASS: mkfile_mmap -n $size -f ${test_file} succeeded"
fi

# make a local file, with the same size
cti_execute_cmd "mkfile_mmap -n $size -f ${testdir}/${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: mkfile_mmap -n $size -f ${testdir}/${test_file} failed"
	return
else
	cti_report "PASS: mkfile_mmap -n $size -f ${testdir}/${test_file} succeeded"
fi

# open(O_RDWR) & mmap(PROT_READ|PROT_WRITE, MAP_SHARED) the smbfs file, verify if can write it
cti_execute_cmd "prot_mmap -o r rw -m rs rws -f ${testdir}/${test_file} ${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: prot_mmap -o r rw -m rs rws -f ${testdir}/${test_file} ${test_file} failed"
	return
else
	cti_report "PASS: prot_mmap -o r rw -m rs rws -f ${testdir}/${test_file} ${test_file} succeeded"
fi

#
cti_execute FAIL "sum ${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: smbfs sum failed"
	return
else
	cti_report "PASS: smbfs sum succeeded"
fi
read sum1 cnt1 junk < cti_stdout

#
cti_execute FAIL "sum ${testdir}/${test_file}"
if (($?!=0)); then
	cti_fail "FAIL: local sum failed"
	return
else
	cti_report "PASS: local sum succeeded"
fi
read sum2 cnt2 junk < cti_stdout

# verify the data has been written back to the smbfs file
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

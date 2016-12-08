#!/bin/ksh
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
# Copyright 2017 Gordon W. Ross
#

errs=
UAKEY=$ROOT/opt/util-tests/bin/oamu_uakey
if [ -n "$ROOT" ] ; then
  export LD_LIBRARY_PATH=$ROOT/usr/lib
fi

#
# Make test data files
#
# uakey.tst (what we should get)
echo '
defrid=123
defgroup=456
defgname=TestGroup
defparent=TestDefParent
defskel=TestDefSkel
defshell=TestDefShell
definact=3
defexpire=TestDefExpire
defauthorization=TestAuths
defprofile=TestProfiles
defrole=TestRoles
defproj=7
defprojname=TestProject
deflimitpriv=TestLimPriv
defdefaultpriv=TestDefPriv
deflock_after_retries=TestLAR' > user.tst


#
# Run some tests
#
# Override all the values we can with non-defaults.
#define	USERATTR_TYPE_KW		"type"
#define	USERATTR_AUTHS_KW		"auths"
#define	USERATTR_PROFILES_KW		"profiles"
#define	USERATTR_ROLES_KW		"roles"
#define	USERATTR_DEFAULTPROJ_KW		"project"
#define	USERATTR_LIMPRIV_KW		"limitpriv"
#define	USERATTR_DFLTPRIV_KW		"defaultpriv"
#define	USERATTR_LOCK_AFTER_RETRIES_KW	"lock_after_retries"
#

$UAKEY	-K auths=TestAuths \
	-K profiles=TestProfiles \
	-K roles=TestRoles \
	-K limitpriv=TestLimPriv \
	-K defaultpriv=TestDefPriv \
	-K lock_after_retries=TestLAR |
  tail +2 > user1.out

if cmp -s user.tst user1.out ; then
    echo "PASS: user1.out"
else
    echo "FAIL: user1.out is wrong"
    errs="$errs user1.out"
fi

rm -f user.tst user1.out

#!/bin/ksh

errs=
OAMUT=$ROOT/opt/util-tests/bin/oamutest
if [ -n "$ROOT" ] ; then
  export LD_LIBRARY_PATH=$ROOT/usr/lib
fi

#
# Make test data files
#

# user.def (same as compiled-in defaults)
echo '
defrid=99
defgroup=1
defgname=other
defparent=/home
defskel=/etc/skel
defshell=/bin/sh
definact=0
defexpire=
defauthorization=
defprofile=
defrole=
defproj=3
defprojname=default
deflimitpriv=
defdefaultpriv=
deflock_after_retries=' > user.def

# user.tst (different from defaults)
echo '
defrid=1001
defgroup=20
defgname=test20group
defparent=/tank/home
defskel=/etc/skel.testuser
defshell=/bin/pfsh.testuser
definact=0
defexpire=
defauthorization=
defprofile=TestUser
defrole=TestRole
defproj=4
defprojname=TestProj
deflimitpriv=TestLimPriv
defdefaultpriv=TestDefPriv
deflock_after_retries=' > user.tst

#
# Run some tests
#

# Override compiled-in user defaults
$OAMUT user.def |tail +2 > user1.out
if cmp -s user.def user1.out ; then
    echo "PASS: user1.out"
else
    echo "FAIL: user1.out is wrong"
    errs="$errs user1.out"
fi

# Override user.def values
$OAMUT user.def user.tst |tail +2 > user2.out
if cmp -s user.tst user2.out ; then
    echo "PASS: user2.out"
else
    echo "FAIL: user2.out is wrong"
    errs="$errs user2.out"
fi

rm -f user.def user.tst user1.out user2.out

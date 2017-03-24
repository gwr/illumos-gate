#!/bin/ksh

errs=
OAMUT=$ROOT/opt/util-tests/bin/oamu_defs
if [ -n "$ROOT" ] ; then
  export LD_LIBRARY_PATH=$ROOT/usr/lib
fi

#
# Make test data files
#

# role.def (same as compiled-in defaults)
echo '
defrid=99
defgroup=1
defgname=other
defparent=/home
defskel=/etc/skel
defshell=/bin/pfsh
definact=0
defexpire=
defauthorization=
defprofile=All
defproj=3
defprojname=default
deflimitpriv=
defdefaultpriv=
deflock_after_retries=' > role.def

# role.tst (different from defaults)
echo '
defrid=101
defgroup=10
defgname=testrole
defparent=/home/testrole
defskel=/etc/skel.testrole
defshell=/bin/pfsh.testrole
definact=0
defexpire=
defauthorization=
defprofile=All,TestRole
defproj=4
defprojname=default
deflimitpriv=
defdefaultpriv=
deflock_after_retries=' > role.tst

#
# Run some tests
#

# Override compiled-in role defaults
$OAMUT -r role.def |tail +2 > role1.out
if cmp -s role.def role1.out ; then
    echo "PASS: role1.out"
else
    echo "FAIL: role1.out is wrong"
    errs="$errs role1.out"
fi

# Override role.def values
$OAMUT -r role.def role.tst |tail +2 > role2.out
if cmp -s role.tst role2.out ; then
    echo "PASS: role2.out"
else
    echo "FAIL: role2.out is wrong"
    errs="$errs role2.out"
fi

rm -f role.def role.tst role1.out role2.out

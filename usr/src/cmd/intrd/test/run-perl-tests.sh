#!/bin/sh
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
# Copyright 2026 Gordon W. Ross
#

#
# Run intrd test scenarios.
#
# Tests the Perl intrd in the parent directory.
# Allows override of $INTRD
#

INTRD=${INTRD:-perl ../intrd.pl}

fail=0
pass=0

TESTS=${@:-case*.kstats}

for tc in $TESTS; do
    name=${tc%.kstats}
    $INTRD -D -S ./intrd-test.pl $tc > $name.out 2>&1
    if diff -u $name.ref $name.out >/dev/null 2>&1; then
        echo "$name PASS"
        rm -f $name.out
        pass=$((pass + 1))
    else
        echo "$name FAIL"
        diff -u $name.ref $name.out
        fail=$((fail + 1))
    fi
done

echo ""
echo "Results: $pass passed, $fail failed"
[ $fail -eq 0 ]
